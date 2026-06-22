"""MapReduce framework for Fly.

Four-stage pipeline: Partition → Process → Reduce/Merge → Finalize.

Usage::

    db = fly.open_db("./mr_db")
    fly.launch_workers([{}, {}, {}])

    mr = MapReduceJob(db, output_name="result")
    mr.set_partitioner(lambda data: [data[i::3] for i in range(3)])
    mr.set_processor(lambda part: sum(part))
    mr.set_merger(lambda a, b: a + b, "summary")
    mr.run([1, 2, 3, 4, 5, 6, 7, 8, 9])
    fly.wait_tasks()
    result = mr.get()

Downstream dependency::

    @fly.as_task(inputs=lambda db, mr: [mr.get_output_name()])
    def downstream(db, mr):
        data = mr.get(db)
"""

import pickle
from uuid import uuid4

try:
    import cloudpickle
except ImportError:
    cloudpickle = None

from fly import as_task

# ---------------------------------------------------------------------------
# Serialization helpers
# ---------------------------------------------------------------------------

def _serialize_fn(fn):
    """Serialize a callable for passing to distributed tasks."""
    serializer = cloudpickle if cloudpickle is not None else pickle
    return serializer.dumps(fn).hex()


def _deserialize_fn(hex_str):
    return pickle.loads(bytes.fromhex(hex_str))


# ---------------------------------------------------------------------------
# Internal task definitions (registered in _task_registry via @as_task)
# ---------------------------------------------------------------------------

@as_task()
def _mr_partition_task(db, job_id, data_hex, partition_fn_hex):
    """Partition input data and write each partition as a temp object."""
    partition_fn = _deserialize_fn(partition_fn_hex)
    data = _deserialize_fn(data_hex)
    partitions = partition_fn(data)
    for i, part in enumerate(partitions):
        db.write_object(f"__mr__{job_id}__part__{i}", part, save_to_db=False)


@as_task(inputs=lambda db, job_id, part_id, partition_key, process_fn_hex: [
    db.get_full_name(partition_key)
])
def _mr_process_task(db, job_id, part_id, partition_key, process_fn_hex):
    """Process a single partition."""
    process_fn = _deserialize_fn(process_fn_hex)
    data = db.read_object(partition_key)
    result = process_fn(data)
    db.write_object(f"__mr__{job_id}__proc__{part_id}", result, save_to_db=False)


# -- Summary merge: multi-stage tree ----------------------------------------

@as_task(inputs=lambda db, job_id, stage, merge_id, input_keys, merge_fn_hex: [
    db.get_full_name(k) for k in input_keys
])
def _mr_summary_merge_task(db, job_id, stage, merge_id, input_keys, merge_fn_hex):
    """Merge multiple items in one tree-reduce step (summary type)."""
    merge_fn = _deserialize_fn(merge_fn_hex)
    items = [db.read_object(k) for k in input_keys]
    merged = items[0]
    for item in items[1:]:
        merged = merge_fn(merged, item)
    db.write_object(
        f"__mr__{job_id}__merge__s{stage}__{merge_id}",
        merged,
        save_to_db=False,
    )


# -- Full merge: single-stage with concurrent reads --------------------------

@as_task(inputs=lambda db, job_id, input_keys, merge_fn_hex, output_name: [
    db.get_full_name(k) for k in input_keys
])
def _mr_full_merge_task(db, job_id, input_keys, merge_fn_hex, output_name):
    """Single-stage full merge with 2-concurrent-read acceleration."""
    from concurrent.futures import ThreadPoolExecutor
    merge_fn = _deserialize_fn(merge_fn_hex)

    # Read all inputs with 2 concurrent readers
    def _read(key):
        return db.read_object(key)

    with ThreadPoolExecutor(max_workers=2) as pool:
        items = list(pool.map(_read, input_keys))

    merged = items[0]
    for item in items[1:]:
        merged = merge_fn(merged, item)

    db.write_object(output_name, merged, save_to_db=False)


# -- Finalize: post-processing on merged result ------------------------------

@as_task(inputs=lambda db, job_id, finalize_fn_hex, output_name, merge_output_key: [
    db.get_full_name(merge_output_key)
])
def _mr_finalize_task(db, job_id, finalize_fn_hex, output_name, merge_output_key):
    """Apply finalize function to merged result, write to output_name (persisted)."""
    finalize_fn = _deserialize_fn(finalize_fn_hex)
    merged = db.read_object(merge_output_key)
    final = finalize_fn(merged)
    db.write_object(output_name, final, save_to_db=True)


# -- Cleanup: remove intermediate temp objects -------------------------------

@as_task(inputs=lambda db, job_id, intermediate_keys, output_name: [
    db.get_full_name(output_name)
])
def _mr_cleanup_task(db, job_id, intermediate_keys, output_name):
    """Remove all intermediate temp objects."""
    for key in intermediate_keys:
        try:
            db.remove_object(key)
        except Exception:
            pass


@as_task(inputs=lambda db, src_key, dst_key: [db.get_full_name(src_key)])
def _mr_copy_to_output(db, src_key, dst_key):
    data = db.read_object(src_key)
    db.write_object(dst_key, data, save_to_db=True)


# ---------------------------------------------------------------------------
# MapReduceJob
# ---------------------------------------------------------------------------

class MapReduceJob:
    """Four-stage MapReduce pipeline: Partition → Process → Merge → Finalize.

    The job object is picklable and can be passed to ``@as_task`` as a
    dependency parameter.  Use ``get_output_name()`` in the ``inputs``
    lambda to declare a dependency on the job result.

    Args:
        db: A ``fly.open_db()`` database instance.
        output_name: Name for the final persisted result.
        keep_intermediate: If True, intermediate temp objects are not removed.
    """

    def __init__(self, db, output_name: str, keep_intermediate: bool = False):
        self._db = db
        self._db_id = db.get_db_id()
        self._output_name = output_name
        self._keep_intermediate = keep_intermediate
        self._job_id = None

        # User-defined functions (set via setters)
        self._partition_fn = None
        self._pre_partitioned_names = None
        self._process_fn = None
        self._merge_fn = None
        self._merge_type = "summary"
        self._finalize_fn = None

        # Internal state (set during run)
        self._num_partitions = 0
        self._intermediate_keys = []

    # ── Configuration (chainable) ───────────────────────────────────────

    def set_partitioner(self, fn):
        """Set partitioning function: fn(iterable) → list[partition_data].

        The function receives the full input and returns a list where each
        element is one partition's data.
        """
        self._partition_fn = fn
        return self

    def set_pre_partitioned(self, names: list):
        """Skip the partition phase. *names* are existing object names in db.

        Each name refers to an already-written object that contains one
        partition's data.
        """
        self._pre_partitioned_names = list(names)
        return self

    def set_processor(self, fn):
        """Set processing function: fn(partition_data) → processed_data."""
        self._process_fn = fn
        return self

    def set_merger(self, fn, merge_type: str = "summary"):
        """Set merge function: fn(a, b) → merged.

        Args:
            fn: Binary merge function.
            merge_type: "summary" for multi-stage tree merge (data stays
                small), or "full" for single-stage merge (data may be large,
                uses 2-concurrent-read acceleration).
        """
        self._merge_fn = fn
        self._merge_type = merge_type
        return self

    def set_finalizer(self, fn):
        """Set finalize function: fn(merged_data) → final_result.  Optional."""
        self._finalize_fn = fn
        return self

    # ── Execution ───────────────────────────────────────────────────────

    def run(self, input_data=None):
        """Submit all MapReduce stages as distributed tasks.

        This method is non-blocking — it submits tasks and returns immediately.

        Args:
            input_data: Input data for partitioning.  Required unless
                ``set_pre_partitioned()`` was called.
        """
        if self._merge_fn is None:
            raise ValueError("Merger must be set via set_merger()")
        if self._process_fn is None:
            raise ValueError("Processor must be set via set_processor()")

        self._job_id = uuid4().hex[:8]
        self._intermediate_keys = []

        # ── Phase 1: Partition ──────────────────────────────────────────
        if self._pre_partitioned_names is not None:
            self._num_partitions = len(self._pre_partitioned_names)
            # Partition keys = the pre-existing object short names
            for name in self._pre_partitioned_names:
                # Strip db_id prefix if present
                short = name.split(":")[-1] if ":" in name else name
                self._intermediate_keys.append(f"__mr__{self._job_id}__part__placeholder")
            # We don't write partition objects — process tasks will read
            # the pre-existing objects directly.  Build partition_keys as
            # the actual object names.
            self._partition_keys = list(self._pre_partitioned_names)
        else:
            if input_data is None:
                raise ValueError("input_data required when no pre_partitioned names set")
            if self._partition_fn is None:
                raise ValueError("Partitioner must be set via set_partitioner() "
                                 "or use set_pre_partitioned()")

            data_hex = _serialize_fn(input_data)
            partition_fn_hex = _serialize_fn(self._partition_fn)
            _mr_partition_task(self._db, self._job_id, data_hex, partition_fn_hex)

            # We don't know num_partitions until partition_fn runs on a worker.
            # So we run it locally just to count — the actual data comes from
            # the remote task.  This is a lightweight "dry run" to determine
            # the task graph shape.
            try:
                dry_partitions = self._partition_fn(input_data)
                self._num_partitions = len(dry_partitions)
            except Exception:
                raise ValueError(
                    "Partition function must accept the input data and return "
                    "a list of partitions.  Failed to dry-run for task graph "
                    "planning.")

            self._partition_keys = [
                f"__mr__{self._job_id}__part__{i}"
                for i in range(self._num_partitions)
            ]
            self._intermediate_keys.extend(self._partition_keys)

        # ── Phase 2: Process ────────────────────────────────────────────
        process_fn_hex = _serialize_fn(self._process_fn)
        for i in range(self._num_partitions):
            _mr_process_task(
                self._db, self._job_id, i,
                self._partition_keys[i], process_fn_hex,
            )

        self._processed_keys = [
            f"__mr__{self._job_id}__proc__{i}"
            for i in range(self._num_partitions)
        ]
        self._intermediate_keys.extend(self._processed_keys)

        # ── Phase 3: Reduce/Merge ──────────────────────────────────────
        if self._merge_type == "summary":
            self._merge_output_key = self._run_summary_merge()
        else:
            self._merge_output_key = self._run_full_merge()

        self._intermediate_keys.append(self._merge_output_key)

        # ── Phase 4: Finalize (optional) ────────────────────────────────
        if self._finalize_fn is not None:
            finalize_fn_hex = _serialize_fn(self._finalize_fn)
            _mr_finalize_task(
                self._db, self._job_id, finalize_fn_hex,
                self._output_name, self._merge_output_key,
            )
            # Final output is output_name; finalize wrote it
        else:
            # No finalize: write merge output directly as the final result
            self._write_merge_as_final()

        # ── Cleanup ─────────────────────────────────────────────────────
        if not self._keep_intermediate:
            _mr_cleanup_task(
                self._db, self._job_id,
                self._intermediate_keys, self._output_name,
            )

    # ── Merge Strategies ────────────────────────────────────────────────

    def _run_summary_merge(self) -> str:
        """Multi-stage tree merge for summary (small-data) merges."""
        merge_fn_hex = _serialize_fn(self._merge_fn)
        fan_in = min(self._num_partitions, 8)

        current_keys = list(self._processed_keys)
        stage = 0

        while len(current_keys) > 1:
            next_keys = []
            merge_id = 0
            for start in range(0, len(current_keys), fan_in):
                batch = current_keys[start:start + fan_in]
                out_key = f"__mr__{self._job_id}__merge__s{stage}__{merge_id}"
                _mr_summary_merge_task(
                    self._db, self._job_id, stage, merge_id,
                    batch, merge_fn_hex,
                )
                next_keys.append(out_key)
                merge_id += 1

            current_keys = next_keys
            stage += 1

        return current_keys[0] if current_keys else self._processed_keys[0]

    def _run_full_merge(self) -> str:
        """Single-stage full merge with concurrent read acceleration."""
        output_key = f"__mr__{self._job_id}__merged"
        merge_fn_hex = _serialize_fn(self._merge_fn)
        _mr_full_merge_task(
            self._db, self._job_id, self._processed_keys,
            merge_fn_hex, output_key,
        )
        return output_key

    def _write_merge_as_final(self):
        if self._finalize_fn is not None:
            return
        _mr_copy_to_output(self._db, self._merge_output_key, self._output_name)

    # ── Result Retrieval ────────────────────────────────────────────────

    def get(self, db=None):
        """Read the final result from the database.

        On the Master side this reads after tasks complete (caller should
        ``fly.wait_tasks()`` first).  On the Worker side (inside a task)
        pass ``db`` explicitly since the pickled MR object loses its
        internal db reference.

        Args:
            db: Optional database override (needed inside worker tasks).
        """
        db = db or self._db
        if db is None:
            raise RuntimeError(
                "No database reference.  Pass db explicitly: mr.get(db)")
        return db.read_object(self._output_name)

    def get_output_name(self) -> str:
        """Return full object name (db_id:output_name) for dependency declaration.

        Use this in ``@as_task(inputs=lambda db, mr: [mr.get_output_name()])``
        to create a dependency on this job's result.
        """
        return f"{self._db_id}:{self._output_name}"

    # ── Pickle support ──────────────────────────────────────────────────

    def __getstate__(self):
        return {
            "_db_id": self._db_id,
            "_output_name": self._output_name,
            "_keep_intermediate": self._keep_intermediate,
            "_job_id": self._job_id,
            "_num_partitions": self._num_partitions,
            "_merge_type": self._merge_type,
        }

    def __setstate__(self, state):
        self.__dict__.update(state)
        self._db = None
        self._partition_fn = None
        self._pre_partitioned_names = None
        self._process_fn = None
        self._merge_fn = None
        self._finalize_fn = None
        self._intermediate_keys = []
        self._partition_keys = []
        self._processed_keys = []
        self._merge_output_key = None
