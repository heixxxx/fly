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

}  // namespace fly