export interface process_info {
    id: number;
    name: string;
    user_name: string;
    mem: number; // 内存占用情况
    cpu: number; // 使用率
}

export interface node_process_watcher {

    /**
     *  监听进程，需要提供一个 key
     * @param key
     * @param call
     * @param pids 监听特定进程信息（只是起到过滤作用)
     */
    on: (key: string, call: (list: process_info[]) => any, pids?: number[]) => void;

    /**
     * 关闭某个key的监听 只有当前系统上所有的key都关闭了，程序才会停止实时监控进程信息
     * @param key
     */
    close: (key: string) => void;

    /**
     * 对某个key的监听进程id切换设置
     * 监听特定进程信息（只是起到过滤作用)
     * @param key
     * @param pids
     */
    pids: (key: string, pids: number[]) => void;

    /**
     * get sys all pid  获取系统所有的进程pid信息
     * @param ppid
     */
    get_all_pid(ppid?: number): { pid: number, cpu: number, ppid: number }[];

    /**
     * kill process 可以关闭所有的子进程
     * @param pid
     * @param kill_all_children default false
     */
    kill_process(pid: number, kill_all_children ?: boolean): void;

    /**
     * 使用子线程遍历全部的文件与数量
     * @param folder_name
     * @param on
     */
    on_folder_size(folder_name: string, on: (file_num: number, total_size: number) => void);

    /**
     * 停止对某个目录的数量统计
     * @param folder_name
     */
    stop_folder_size(folder_name: string);


    get_system_proxy_for_windows(): {
        enabled: boolean
        ip: boolean
        port: number
        bypass?: string
        useForLocal?: boolean
    };

    set_system_proxy_for_windows(data: {
        enabled: boolean
        ip: boolean
        port: number
        bypass?: string
        useForLocal?: boolean
    }): boolean;

    get_system_proxy_for_mac(): {
        name: string
        proxies: {
            type: number // 1 http, 2 https
            enabled: boolean
            ip: string
            port: string
            bypass?: string
            useForLocal?: boolean
        }
        bypass?: string
    }[];

    set_system_proxy_for_mac(data: {
        name: string
        proxies: {
            type: number // 1 http, 2 https
            enabled: boolean
            ip: string
            port: string
            bypass?: string
            useForLocal?: boolean
        }
        bypass?: string
    }[]): boolean;
}

export declare const node_process_watcher: node_process_watcher;
