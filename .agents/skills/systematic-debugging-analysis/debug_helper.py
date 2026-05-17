#!/usr/bin/env python3
"""
Fly项目调试工具包
提供系统化调试的辅助功能
"""

import os
import sys
import re
import glob
import time
from datetime import datetime, timedelta
from typing import List, Dict, Optional, Tuple

class FlyDebugHelper:
    """Fly项目调试助手"""
    
    def __init__(self, log_dir: str = "logs/", build_dir: str = "bazel-bin/"):
        self.log_dir = log_dir
        self.build_dir = build_dir
        self.log_files = self._find_log_files()
    
    def _find_log_files(self) -> Dict[str, str]:
        """查找所有日志文件"""
        log_files = {}
        
        if os.path.exists(self.log_dir):
            for file in os.listdir(self.log_dir):
                if file.endswith('.log'):
                    component = file.replace('.log', '')
                    log_files[component] = os.path.join(self.log_dir, file)
        
        return log_files
    
    def analyze_logs_by_component(self, component: str) -> Dict:
        """分析特定组件的日志"""
        if component not in self.log_files:
            return {"error": f"Log file not found for component: {component}"}
        
        log_file = self.log_files[component]
        analysis = {
            "file_path": log_file,
            "size": os.path.getsize(log_file),
            "line_count": 0,
            "error_count": 0,
            "warning_count": 0,
            "info_count": 0,
            "debug_count": 0,
            "recent_errors": [],
            "timeline": []
        }
        
        try:
            with open(log_file, 'r', encoding='utf-8') as f:
                for line_num, line in enumerate(f, 1):
                    analysis["line_count"] += 1
                    
                    # 分析日志级别
                    if "ERROR" in line:
                        analysis["error_count"] += 1
                        analysis["recent_errors"].append({
                            "line": line_num,
                            "content": line.strip(),
                            "timestamp": self._extract_timestamp(line)
                        })
                    elif "WARN" in line:
                        analysis["warning_count"] += 1
                    elif "INFO" in line:
                        analysis["info_count"] += 1
                    elif "DEBUG" in line:
                        analysis["debug_count"] += 1
                    
                    # 记录时间线（最近100条）
                    if len(analysis["timeline"]) < 100:
                        analysis["timeline"].append({
                            "line": line_num,
                            "content": line.strip(),
                            "timestamp": self._extract_timestamp(line)
                        })
        
        except Exception as e:
            analysis["error"] = f"Failed to read log file: {str(e)}"
        
        return analysis
    
    def _extract_timestamp(self, line: str) -> Optional[str]:
        """从日志行中提取时间戳"""
        # 匹配格式: [2026-05-16 14:30:45.123]
        match = re.search(r'\[([^\]]+)\]', line)
        if match:
            return match.group(1)
        return None
    
    def analyze_master_worker_interaction(self, master_id: str = "master", worker_id: int = 1) -> Dict:
        """分析Master-Worker交互"""
        analysis = {
            "master_log": None,
            "worker_log": None,
            "interaction_timeline": [],
            "connection_events": [],
            "task_events": [],
            "heartbeat_events": [],
            "errors": []
        }
        
        # 分析Master日志
        if master_id in self.log_files:
            analysis["master_log"] = self.analyze_logs_by_component(master_id)
            self._extract_interaction_events(analysis["master_log"], "master")
        
        # 分析Worker日志
        worker_key = f"worker{worker_id}"
        if worker_key in self.log_files:
            analysis["worker_log"] = self.analyze_logs_by_component(worker_key)
            self._extract_interaction_events(analysis["worker_log"], "worker")
        
        # 交互分析
        self._analyze_interaction_timeline(analysis)
        
        return analysis
    
    def _extract_interaction_events(self, log_analysis: Dict, component: str):
        """提取交互事件"""
        for entry in log_analysis.get("timeline", []):
            content = entry["content"]
            timestamp = entry["timestamp"]
            
            # 连接相关事件
            if "connect" in content.lower() or "register" in content.lower():
                log_analysis["connection_events"].append({
                    "timestamp": timestamp,
                    "content": content,
                    "component": component
                })
            
            # 任务相关事件
            if "task" in content.lower() and ("assign" in content.lower() or "complete" in content.lower()):
                log_analysis["task_events"].append({
                    "timestamp": timestamp,
                    "content": content,
                    "component": component
                })
            
            # 心跳相关事件
            if "heartbeat" in content.lower():
                log_analysis["heartbeat_events"].append({
                    "timestamp": timestamp,
                    "content": content,
                    "component": component
                })
            
            # 错误事件
            if "ERROR" in content:
                log_analysis["errors"].append({
                    "timestamp": timestamp,
                    "content": content,
                    "component": component
                })
    
    def _analyze_interaction_timeline(self, analysis: Dict):
        """分析交互时间线"""
        master_events = []
        worker_events = []
        
        # 收集所有事件
        if analysis["master_log"]:
            for event_type in ["connection_events", "task_events", "heartbeat_events", "errors"]:
                for event in analysis["master_log"].get(event_type, []):
                    event["type"] = event_type
                    master_events.append(event)
        
        if analysis["worker_log"]:
            for event_type in ["connection_events", "task_events", "heartbeat_events", "errors"]:
                for event in analysis["worker_log"].get(event_type, []):
                    event["type"] = event_type
                    worker_events.append(event)
        
        # 按时间排序所有事件
        all_events = master_events + worker_events
        all_events.sort(key=lambda x: x["timestamp"] or "")
        
        analysis["interaction_timeline"] = all_events[-50:]  # 最近50个事件
        analysis["timeline_analysis"] = self._analyze_event_patterns(all_events)
    
    def _analyze_event_patterns(self, events: List[Dict]) -> Dict:
        """分析事件模式"""
        analysis = {
            "total_events": len(events),
            "event_types": {},
            "component_activity": {"master": 0, "worker": 0},
            "timeline_gaps": [],
            "potential_issues": []
        }
        
        # 统计事件类型和组件活动
        for event in events:
            event_type = event["type"]
            component = event["component"]
            
            analysis["event_types"][event_type] = analysis["event_types"].get(event_type, 0) + 1
            analysis["component_activity"][component] += 1
        
        # 分析时间间隙
        for i in range(1, len(events)):
            if events[i]["timestamp"] and events[i-1]["timestamp"]:
                try:
                    time1 = datetime.strptime(events[i-1]["timestamp"], "%Y-%m-%d %H:%M:%S.%f")
                    time2 = datetime.strptime(events[i]["timestamp"], "%Y-%m-%d %H:%M:%S.%f")
                    gap = (time2 - time1).total_seconds()
                    
                    if gap > 10:  # 超过10秒的间隙
                        analysis["timeline_gaps"].append({
                            "start_time": events[i-1]["timestamp"],
                            "end_time": events[i]["timestamp"],
                            "duration": gap
                        })
                except:
                    pass
        
        # 检测潜在问题
        if analysis["component_activity"]["master"] == 0 and analysis["component_activity"]["worker"] > 0:
            analysis["potential_issues"].append("Worker有活动但Master无记录，可能存在连接问题")
        
        if len(analysis["timeline_gaps"]) > 5:
            analysis["potential_issues"].append("发现多个长时间时间间隙，可能存在性能问题")
        
        return analysis
    
    def run_test_with_logging(self, test_target: str, log_level: str = "DEBUG") -> Dict:
        """运行测试并生成详细日志"""
        print(f"🧪 Running test: {test_target}")
        print(f"📝 Log level: {log_level}")
        
        # 清理旧日志
        if os.path.exists(self.log_dir):
            import shutil
            shutil.rmtree(self.log_dir)
        
        # 创建日志目录
        os.makedirs(self.log_dir, exist_ok=True)
        
        # 运行测试
        cmd = f"./fly.sh test {test_target}"
        print(f"⚡ Executing: {cmd}")
        
        import subprocess
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        
        # 重新扫描日志文件
        self.log_files = self._find_log_files()
        
        return {
            "command": cmd,
            "return_code": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
            "log_files": self.log_files,
            "test_status": "PASSED" if result.returncode == 0 else "FAILED"
        }
    
    def generate_debug_report(self, components: List[str] = None) -> str:
        """生成调试报告"""
        if components is None:
            components = list(self.log_files.keys())
        
        report = []
        report.append("=" * 60)
        report.append("🔍 Fly项目调试报告")
        report.append("=" * 60)
        report.append(f"📁 日志目录: {self.log_dir}")
        report.append(f"📊 分析组件: {', '.join(components)}")
        report.append(f"📅 生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        report.append("")
        
        for component in components:
            if component in self.log_files:
                analysis = self.analyze_logs_by_component(component)
                report.append(f"📋 组件: {component}")
                report.append(f"   📄 文件: {analysis['file_path']}")
                report.append(f"   📏 大小: {analysis['size']} bytes")
                report.append(f"   📝 行数: {analysis['line_count']}")
                report.append(f"   ⚠️  错误数: {analysis['error_count']}")
                report.append(f"   ⚠️  警告数: {analysis['warning_count']}")
                report.append(f"   ℹ️  信息数: {analysis['info_count']}")
                report.append(f"   🔍 调试数: {analysis['debug_count']}")
                
                if analysis['recent_errors']:
                    report.append("   🚨 最近错误:")
                    for error in analysis['recent_errors'][:5]:  # 最近5个错误
                        report.append(f"     - 行{error['line']}: {error['content'][:100]}...")
                
                report.append("")
        
        # Master-Worker交互分析
        if "master" in self.log_files:
            mw_analysis = self.analyze_master_worker_interaction()
            if mw_analysis["timeline_analysis"]["potential_issues"]:
                report.append("🚨 潜在问题:")
                for issue in mw_analysis["timeline_analysis"]["potential_issues"]:
                    report.append(f"   - {issue}")
                report.append("")
        
        return "\n".join(report)


def main():
    """主函数 - 命令行接口"""
    import argparse
    
    parser = argparse.ArgumentParser(description="Fly项目调试工具")
    parser.add_argument("--log-dir", default="logs/", help="日志目录")
    parser.add_argument("--component", help="分析特定组件")
    parser.add_argument("--test", help="运行指定测试")
    parser.add_argument("--log-level", default="DEBUG", help="日志级别")
    parser.add_argument("--master", default="master", help="Master组件名称")
    parser.add_argument("--worker", type=int, default=1, help="Worker ID")
    parser.add_argument("--report", action="store_true", help="生成调试报告")
    
    args = parser.parse_args()
    
    helper = FlyDebugHelper(log_dir=args.log_dir)
    
    if args.test:
        result = helper.run_test_with_logging(args.test, args.log_level)
        print(f"测试结果: {result['test_status']}")
        if result['stderr']:
            print("错误输出:")
            print(result['stderr'])
        
        # 重新生成报告
        if args.report:
            report = helper.generate_debug_report()
            print("\n" + report)
    
    elif args.component:
        analysis = helper.analyze_logs_by_component(args.component)
        print(f"📋 {args.component} 组件分析:")
        print(f"   📄 文件: {analysis['file_path']}")
        print(f"   📏 大小: {analysis['size']} bytes")
        print(f"   📝 行数: {analysis['line_count']}")
        print(f"   ⚠️  错误数: {analysis['error_count']}")
        print(f"   ⚠️  警告数: {analysis['warning_count']}")
        print(f"   ℹ️  信息数: {analysis['info_count']}")
        print(f"   🔍 调试数: {analysis['debug_count']}")
    
    elif args.report:
        report = helper.generate_debug_report()
        print(report)
    
    else:
        # 默认分析所有组件
        report = helper.generate_debug_report()
        print(report)


if __name__ == "__main__":
    main()