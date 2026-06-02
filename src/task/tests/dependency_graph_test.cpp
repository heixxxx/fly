#include <gtest/gtest.h>
#include <task/cpp/dependency_graph.h>

namespace fly {

TEST(DependencyGraphTest, AddTaskWithNoDependencies) {
    DependencyGraph graph;
    graph.add_task(1, {});
    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0], 1);
}

TEST(DependencyGraphTest, AddTaskWithDependencies) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a"});
    graph.add_task(2, {"input/b"});
    
    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 0);
    
    graph.mark_data_ready("input/a");
    ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0], 1);
}

TEST(DependencyGraphTest, MultipleDependencies) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a", "input/b"});
    
    graph.mark_data_ready("input/a");
    EXPECT_FALSE(graph.is_task_ready(1));
    
    graph.mark_data_ready("input/b");
    EXPECT_TRUE(graph.is_task_ready(1));
    
    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
}

TEST(DependencyGraphTest, RemoveTask) {
    DependencyGraph graph;
    graph.add_task(1, {});
    graph.remove_task(1);
    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 0);
}

TEST(DependencyGraphTest, CascadingDependencies) {
    DependencyGraph graph;
    graph.add_task(1, {});
    graph.add_task(2, {"output/1"});
    graph.add_task(3, {"output/2"});
    
    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0], 1);
    
    graph.remove_task(1);
    graph.mark_data_ready("output/1");
    
    ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0], 2);
}

TEST(DependencyGraphTest, AddTaskWithRequirements) {
    DependencyGraph graph;
    graph.add_task(1, {}, {"gpu", "cuda"});
    
    auto reqs = graph.get_task_requirements(1);
    EXPECT_EQ(reqs.size(), 2u);
    EXPECT_EQ(reqs[0], "gpu");
    EXPECT_EQ(reqs[1], "cuda");
}

TEST(DependencyGraphTest, GetRequirementsNonExistent) {
    DependencyGraph graph;
    auto reqs = graph.get_task_requirements(999);
    EXPECT_TRUE(reqs.empty());
}

TEST(DependencyGraphTest, RequirementsClearedOnRemove) {
    DependencyGraph graph;
    graph.add_task(1, {}, {"gpu"});
    graph.remove_task(1);
    
    auto reqs = graph.get_task_requirements(1);
    EXPECT_TRUE(reqs.empty());
}

TEST(DependencyGraphTest, NoRequirementsDefault) {
    DependencyGraph graph;
    graph.add_task(1, {});
    
    auto reqs = graph.get_task_requirements(1);
    EXPECT_TRUE(reqs.empty());
}

TEST(DependencyGraphTest, MarkDataRemovedMovesReadyTaskBackToPending) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a"});
    EXPECT_EQ(graph.get_ready_tasks().size(), 0);

    graph.mark_data_ready("input/a");
    EXPECT_TRUE(graph.is_task_ready(1));
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);

    graph.mark_data_removed("input/a");
    EXPECT_FALSE(graph.is_task_ready(1));
    EXPECT_EQ(graph.get_ready_tasks().size(), 0);

    auto pending = graph.get_pending_tasks();
    EXPECT_EQ(pending.size(), 1);
    EXPECT_EQ(pending[0], 1);
}

TEST(DependencyGraphTest, MarkDataRemovedNoOpWhenNoReadyTasks) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a"});
    EXPECT_EQ(graph.get_ready_tasks().size(), 0);

    graph.mark_data_removed("input/a");
    EXPECT_EQ(graph.get_ready_tasks().size(), 0);
    EXPECT_EQ(graph.get_pending_tasks().size(), 1);
}

TEST(DependencyGraphTest, MarkDataRemovedNoOpWhenNoTaskDependsOnIt) {
    DependencyGraph graph;
    graph.add_task(1, {});
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);

    graph.mark_data_removed("unrelated/path");
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);
    EXPECT_TRUE(graph.is_task_ready(1));
}

TEST(DependencyGraphTest, MarkDataRemovedPartialDeps) {
    DependencyGraph graph;
    graph.add_task(1, {"dep/a", "dep/b"});
    EXPECT_FALSE(graph.is_task_ready(1));

    graph.mark_data_ready("dep/a");
    EXPECT_FALSE(graph.is_task_ready(1));

    graph.mark_data_ready("dep/b");
    EXPECT_TRUE(graph.is_task_ready(1));

    graph.mark_data_removed("dep/a");
    EXPECT_FALSE(graph.is_task_ready(1));
    EXPECT_EQ(graph.get_pending_tasks().size(), 1);
}

TEST(DependencyGraphTest, MarkDataRemovedMultipleTasks) {
    DependencyGraph graph;
    graph.add_task(1, {"shared/input"});
    graph.add_task(2, {"shared/input"});
    graph.add_task(3, {});

    EXPECT_EQ(graph.get_ready_tasks().size(), 1);

    graph.mark_data_ready("shared/input");
    EXPECT_EQ(graph.get_ready_tasks().size(), 3);

    graph.mark_data_removed("shared/input");
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);
    EXPECT_TRUE(graph.is_task_ready(3));
    EXPECT_EQ(graph.get_pending_tasks().size(), 2);
}

TEST(DependencyGraphTest, MarkDataRemovedThenReadyAgain) {
    DependencyGraph graph;
    graph.add_task(1, {"input/x"});

    graph.mark_data_ready("input/x");
    EXPECT_TRUE(graph.is_task_ready(1));

    graph.mark_data_removed("input/x");
    EXPECT_FALSE(graph.is_task_ready(1));

    graph.mark_data_ready("input/x");
    EXPECT_TRUE(graph.is_task_ready(1));
}

TEST(DependencyGraphTest, MarkDataRemovedNonExistentPath) {
    DependencyGraph graph;
    graph.add_task(1, {});
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);

    graph.mark_data_removed("never/existed");
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);
}

TEST(DependencyGraphTest, MarkDataReadyIdempotent) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a"});

    graph.mark_data_ready("input/a");
    EXPECT_TRUE(graph.is_task_ready(1));

    graph.mark_data_ready("input/a");
    EXPECT_TRUE(graph.is_task_ready(1));
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);
}

TEST(DependencyGraphTest, IsDataReadyForMissingPath) {
    DependencyGraph graph;
    EXPECT_FALSE(graph.is_data_ready("never/added"));
}

TEST(DependencyGraphTest, IsTaskReadyForNonExistentTask) {
    DependencyGraph graph;
    EXPECT_FALSE(graph.is_task_ready(999));
}

TEST(DependencyGraphTest, RemoveTaskNonExistent) {
    DependencyGraph graph;
    EXPECT_NO_THROW(graph.remove_task(999));
    EXPECT_EQ(graph.get_ready_tasks().size(), 0);
    EXPECT_EQ(graph.get_pending_tasks().size(), 0);
}

TEST(DependencyGraphTest, GetTaskDependenciesForExistingTask) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a", "input/b"}, {"gpu"});
    auto deps = graph.get_task_dependencies(1);
    EXPECT_EQ(deps.size(), 2);
    EXPECT_EQ(deps[0], "input/a");
    EXPECT_EQ(deps[1], "input/b");
}

TEST(DependencyGraphTest, GetTaskDependenciesForNonExistent) {
    DependencyGraph graph;
    auto deps = graph.get_task_dependencies(999);
    EXPECT_TRUE(deps.empty());
}

TEST(DependencyGraphTest, AddTaskWithRequirementsAndNoDeps) {
    DependencyGraph graph;
    graph.add_task(1, {}, {"gpu", "cuda"});
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);
    auto reqs = graph.get_task_requirements(1);
    EXPECT_EQ(reqs.size(), 2);
}

TEST(DependencyGraphTest, MarkDataReadyForPathNoTaskDependsOn) {
    DependencyGraph graph;
    graph.mark_data_ready("orphan/path");
    EXPECT_TRUE(graph.is_data_ready("orphan/path"));
    EXPECT_EQ(graph.get_ready_tasks().size(), 0);
}

}  // namespace fly
