#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>

class ProcessInfo {
public:
    ProcessInfo();

    static CMSharedPtr<ProcessInfo> instance();

    void set_worker_mode(bool mode) { worker_mode_ = mode; }
    void set_worker_id(int id) { worker_id_ = id; }
    void set_master_port(int port) { master_port_ = port; }
    void set_cli_master_port(int port) { cli_master_port_ = port; }
    void set_master_host(const CMString& host) { master_host_ = host; }
    void set_data_server_host(const CMString& host) { data_server_host_ = host; }
    void set_script_path(const CMString& path) { script_path_ = path; }
    void set_interactive(bool mode) { interactive_ = mode; }
    void set_worker_attributes(const CMString& attrs) { worker_attributes_ = attrs; }
    void set_hostname(const CMString& hostname) { hostname_ = hostname; }

    bool worker_mode() const { return worker_mode_; }
    int worker_id() const { return worker_id_; }
    int master_port() const { return master_port_; }
    int cli_master_port() const { return cli_master_port_; }
    const CMString& master_host() const { return master_host_; }
    const CMString& data_server_host() const { return data_server_host_; }
    const CMString& script_path() const { return script_path_; }
    bool interactive() const { return interactive_; }
    const CMString& worker_attributes() const { return worker_attributes_; }

    CMString hostname() const;

    void reset();

private:
    ProcessInfo(const ProcessInfo&) = delete;
    ProcessInfo& operator=(const ProcessInfo&) = delete;

    bool worker_mode_ = false;
    int worker_id_ = 0;
    int master_port_ = 8000;
    int cli_master_port_ = 0;
    CMString master_host_ = "127.0.0.1";
    CMString data_server_host_ = "127.0.0.1";
    CMString script_path_;
    bool interactive_ = false;
    CMString worker_attributes_;
    mutable CMString hostname_;
};
