#include <gtest/gtest.h>
#include <storage/cpp/database.h>
#include <filesystem>
#include <fstream>

namespace {

class DatabaseTest : public ::testing::Test {
protected:
    CMString test_dir_;

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_db_" + std::to_string(::getpid());
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(DatabaseTest, WriteAndReadObject) {
    CMString base_path = test_dir_ + "/write_read";
    Database db(base_path);

    db.write_object("test/obj", "hello world", false);
    CMString result = db.read_object("test/obj");

    EXPECT_EQ(result, "hello world");
}

TEST_F(DatabaseTest, FreezePreventsWrite) {
    CMString base_path = test_dir_ + "/freeze_prevent";
    Database db(base_path);

    db.write_object("test/obj", "data", false);
    db.freeze();

    EXPECT_TRUE(db.is_frozen());
    EXPECT_THROW(db.write_object("test/obj2", "data2", false), std::runtime_error);
}

TEST_F(DatabaseTest, FrozenMarkerCreated) {
    CMString base_path = test_dir_ + "/frozen_marker";
    Database db(base_path);

    db.freeze();

    std::ifstream ifs(base_path + "/_FROZEN");
    EXPECT_TRUE(ifs.good());
}

TEST_F(DatabaseTest, DoublePathReadPriority) {
    CMString base_path = test_dir_ + "/shared_base";
    CMString data_path = test_dir_ + "/local_data";
    Database db(base_path, data_path);

    db.write_object("priority/test", "local_data", false);
    CMString result = db.read_object("priority/test");

    EXPECT_EQ(result, "local_data");
}

TEST_F(DatabaseTest, LoadMetaFromFrozenDatabase) {
    CMString base_path = test_dir_ + "/meta_db";
    Database db(base_path);

    db.write_object("test/obj", "data", false);
    db.freeze();

    DbMeta meta = db.load_meta();
    EXPECT_EQ(meta.db_id, base_path);
    EXPECT_EQ(meta.base_path, base_path);
    EXPECT_GT(meta.frozen_at, 0);
}

TEST_F(DatabaseTest, GetDbIdReturnsBasePath) {
    CMString base_path = test_dir_ + "/id_check";
    Database db(base_path);

    EXPECT_EQ(db.get_db_id(), base_path);
}

TEST_F(DatabaseTest, GetBasePath) {
    CMString base_path = test_dir_ + "/path_check";
    Database db(base_path);

    EXPECT_EQ(db.get_base_path(), base_path);
}

TEST_F(DatabaseTest, GetDataPath) {
    CMString base_path = test_dir_ + "/data_path_base";
    CMString data_path = test_dir_ + "/data_path_local";
    Database db(base_path, data_path);

    EXPECT_EQ(db.get_data_path(), data_path);
}

TEST_F(DatabaseTest, ResetClearsFrozenState) {
    CMString base_path = test_dir_ + "/reset_db";
    Database db(base_path);

    db.freeze();
    EXPECT_TRUE(db.is_frozen());

    db.reset();
    EXPECT_FALSE(db.is_frozen());

    std::filesystem::path frozen_marker(base_path + "/_FROZEN");
    EXPECT_FALSE(std::filesystem::exists(frozen_marker));
}

TEST_F(DatabaseTest, WriteMultipleObjects) {
    CMString base_path = test_dir_ + "/multi_write";
    Database db(base_path);

    db.write_object("obj1", "data1", false);
    db.write_object("obj2", "data2", false);
    db.write_object("obj3", "data3", false);

    EXPECT_EQ(db.read_object("obj1"), "data1");
    EXPECT_EQ(db.read_object("obj2"), "data2");
    EXPECT_EQ(db.read_object("obj3"), "data3");
}

TEST_F(DatabaseTest, ReadNonexistentObjectThrows) {
    CMString base_path = test_dir_ + "/nonexist";
    Database db(base_path);

    EXPECT_THROW(db.read_object("no/such/object"), std::runtime_error);
}

// ─── Typed write/read tests ───

TEST_F(DatabaseTest, WriteAndReadTypedObject) {
    CMString base_path = test_dir_ + "/typed";
    Database db(base_path);

    CMString data = "typed_data_content";
    CMString result = db.write_object_typed("typed/obj", data, "TestType");
    EXPECT_GT(result.size(), 0);

    ReadResult read_result = db.read_object_typed("typed/obj");
    CMString read_data(read_result.data_buffer.begin(), read_result.data_buffer.end());
    EXPECT_EQ(read_data, data);
    EXPECT_EQ(read_result.py_name, "TestType");
}

TEST_F(DatabaseTest, TypedObjectPersistenceAcrossFlush) {
    CMString base_path = test_dir_ + "/typed_flush";
    Database db(base_path);

    CMString data = "persistent_data";
    db.write_object_typed("persist/obj", data, "PersistType");

    // Re-read without explicit flush (read_object_typed flushes internally)
    ReadResult result = db.read_object_typed("persist/obj");
    CMString read_data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(read_data, data);
    EXPECT_EQ(result.py_name, "PersistType");
}

TEST_F(DatabaseTest, TypedObjectWithPyNameDetection) {
    CMString base_path = test_dir_ + "/typed_pyname";
    Database db(base_path);

    // Write with a specific py_name
    db.write_object_typed("named/obj", "some_data", "MyCustomType");

    // Read and verify py_name is preserved
    ReadResult result = db.read_object_typed("named/obj");
    EXPECT_EQ(result.py_name, "MyCustomType");
    CMString read_data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(read_data, "some_data");
}

TEST_F(DatabaseTest, MultipleTypedObjects) {
    CMString base_path = test_dir_ + "/typed_multi";
    Database db(base_path);

    db.write_object_typed("type/a", "data_a", "TypeA");
    db.write_object_typed("type/b", "data_b", "TypeB");

    ReadResult ra = db.read_object_typed("type/a");
    EXPECT_EQ(ra.py_name, "TypeA");
    CMString da(ra.data_buffer.begin(), ra.data_buffer.end());
    EXPECT_EQ(da, "data_a");

    ReadResult rb = db.read_object_typed("type/b");
    EXPECT_EQ(rb.py_name, "TypeB");
    CMString db2(rb.data_buffer.begin(), rb.data_buffer.end());
    EXPECT_EQ(db2, "data_b");
}

TEST_F(DatabaseTest, TypedNonexistentObjectThrows) {
    CMString base_path = test_dir_ + "/typed_nonexist";
    Database db(base_path);

    EXPECT_THROW(db.read_object_typed("no/such/typed/object"), std::runtime_error);
}

}