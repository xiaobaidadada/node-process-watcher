export interface process_info {
    id: number;
    name: string;
    user_name: string;
    mem: number; // 内存占用情况
    cpu: number; // 使用率
}

export interface node_process_monitor {

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
}

declare const node_process_monitor:node_process_monitor;
export default node_process_monitor; // 导出默认变量