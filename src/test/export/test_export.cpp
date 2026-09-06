#include <export/cpp/export_macros.h>
#include <common/serialization/cpp/serialization_macros.h>
#include <test/cpp/test_object.h>
#include <storage/cpp/database.h>
#include <storage/cpp/data_service.h>
#include <thread>
#include <atomic>
#include <vector>
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
    auto db = fly_export::cast<CMSharedPtr<Database>>(py_db.attr("_db"));
    std::atomic<int64_t> total_sum{0};
    std::atomic<int> local_count{0};
    std::atomic<int> remote_count{0};

    std::vector<std::thread> threads;
    for (size_t i = 0; i < names.size(); i++) {
        threads.emplace_back([db, &names, i, &total_sum, &local_count, &remote_count]() {
            auto full = db->get_full_name(names[i]);
            auto ds = fly::DataService::instance();
            bool was_local = ds->has_local_object(full);

            auto obj = db->read_object<TestObject>(names[i]);
            total_sum += obj->value;
            if (was_local) {
                local_count++;
            } else {
                remote_count++;
            }
        });
    }

    for (auto& t : threads) t.join();

    return fly_export::make_tuple(
        total_sum.load(),
        local_count.load(),
        remote_count.load());
});

}
