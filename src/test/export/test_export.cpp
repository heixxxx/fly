#include <export/cpp/export_macros.h>
#include <serialization/cpp/serialization_macros.h>
#include <test/cpp/test_object.h>
#include <storage/cpp/database.h>
#include <storage/cpp/data_service.h>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <nanobind/stl/pair.h>

FLY_EXPORT_MODULE(_fly_test) {

FLY_EXPORT_CLASS(TestObject, "EXTestObject")
    FLY_EXPORT_INIT()
    FLY_EXPORT_INIT(int64_t, const CMString&)
    FLY_EXPORT_ATTR("value", &TestObject::value)
    FLY_EXPORT_ATTR("name", &TestObject::name)
    FLY_EXPORT_SERIALIZE(TestObject);

FLY_EXPORT_FUNCTION("ex_test_parallel_read",
    [](fly_export::object py_db,
       const CMVector<CMString>& names) -> fly_export::tuple {
    auto& ds = fly::DataService::instance();
    std::atomic<int64_t> total_sum{0};
    std::atomic<int> local_count{0};
    std::atomic<int> remote_count{0};
    std::vector<std::thread> threads;

    {
        fly_export::gil_scoped_release gil_release;

        for (size_t i = 0; i < names.size(); i++) {
            threads.emplace_back([py_db, &ds, &names, i, &total_sum, &local_count, &remote_count]() {
                const auto& name = names[i];
                auto [found, result] = ds.try_read_local(name);

                if (found) {
                    TestObject obj;
                    CMString data(result.data_buffer.begin(), result.data_buffer.end());
                    FLY_DECODE(data, TestObject, obj);
                    total_sum += obj.value;
                    local_count++;
                } else {
                    fly_export::gil_scoped_acquire gil_acquire;
                    auto py_result = py_db.attr("read_object")(name);
                    int64_t val = fly_export::cast<int64_t>(py_result.attr("value"));
                    total_sum += val;
                    remote_count++;
                }
            });
        }

        for (auto& t : threads) t.join();
    }

    return fly_export::make_tuple(
        total_sum.load(),
        local_count.load(),
        remote_count.load());
});

}
