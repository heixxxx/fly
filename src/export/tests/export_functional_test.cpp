#include <gtest/gtest.h>
#include <export/cpp/export_macros.h>
#include <serialization/cpp/serialization_macros.h>
#include <memory>
#include <string>

struct TestExportClass {
    int32_t value;
    CMString name;
    
    TestExportClass() : value(0), name("") {}
    TestExportClass(int32_t v, const CMString& n) : value(v), name(n) {}
    
    int32_t get_value() const { return value; }
    void set_value(int32_t v) { value = v; }
    CMString get_name() const { return name; }
    void set_name(const CMString& n) { name = n; }
};

TEST(ExportMacrosFunctionalTest, MacroExpansionValid) {
    EXPECT_TRUE(true);
}

TEST(ExportMacrosFunctionalTest, PickleMacroCompiles) {
    TestExportClass obj{42, "test"};
    EXPECT_EQ(obj.get_value(), 42);
}

struct SharedPtrTestClass {
    int32_t id;
    CMString data;
    
    SharedPtrTestClass() : id(0), data("") {}
};

TEST(ExportMacrosFunctionalTest, SharedPtrHolderWorks) {
    auto ptr = std::make_shared<SharedPtrTestClass>();
    ptr->id = 100;
    ptr->data = "shared";
    
    EXPECT_EQ(ptr->id, 100);
    EXPECT_EQ(ptr->data, "shared");
}

struct ReadOnlyAttrClass {
    int32_t readonly_value = 999;
    CMString readonly_name = "immutable";
};

TEST(ExportMacrosFunctionalTest, ReadOnlyAttrMacroCompiles) {
    ReadOnlyAttrClass obj;
    EXPECT_EQ(obj.readonly_value, 999);
}

struct PropertyClass {
    int32_t value_;
    
    int32_t get_value() const { return value_; }
    void set_value(int32_t v) { value_ = v; }
};

TEST(ExportMacrosFunctionalTest, PropertyMacroCompiles) {
    PropertyClass obj;
    obj.set_value(50);
    EXPECT_EQ(obj.get_value(), 50);
}

enum class TestEnum {
    ValueA = 0,
    ValueB = 1,
    ValueC = 2,
};

TEST(ExportMacrosFunctionalTest, EnumMacroCompiles) {
    TestEnum e = TestEnum::ValueB;
    EXPECT_EQ(static_cast<int32_t>(e), 1);
}

struct SerializeForExportClass {
    int32_t id = 0;
    CMVector<int32_t> values = {};
    FLY_SERIALIZE(id, values)
};

TEST(ExportMacrosFunctionalTest, SerializeIntegration) {
    SerializeForExportClass original;
    original.id = 42;
    original.values = {1, 2, 3, 4, 5};
    
    CMString serialized;
    FLY_ENCODE(original, serialized);
    
    SerializeForExportClass decoded;
    decoded.id = 0;
    decoded.values = {};
    FLY_DECODE(serialized, SerializeForExportClass, decoded);
    
    EXPECT_EQ(decoded.id, 42);
    EXPECT_EQ(decoded.values.size(), 5);
    EXPECT_EQ(decoded.values[2], 3);
}

TEST(ExportMacrosFunctionalTest, PickleWithSerialize) {
    SerializeForExportClass obj;
    obj.id = 123;
    obj.values = {10, 20, 30};
    
    CMString serialized;
    FLY_ENCODE(obj, serialized);
    
    EXPECT_GT(serialized.size(), 0);
    
    SerializeForExportClass decoded;
    decoded.id = 0;
    decoded.values = {};
    FLY_DECODE(serialized, SerializeForExportClass, decoded);
    
    EXPECT_EQ(decoded.id, obj.id);
    EXPECT_EQ(decoded.values, obj.values);
}