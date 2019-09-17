#ifndef SKYNET_IMP_H
#define SKYNET_IMP_H

// skynet配置
struct skynet_config {
	int thread; // 业务处理线程
	int harbor; // harbor编号
	int profile; // 是否开启profile
	const char * daemon; // 是否守护进程
	const char * module_path; // cpath路径
	const char * bootstrap; // 启动命令: snlua bootstrap
	const char * logger;
	const char * logservice; // 
};

#define THREAD_WORKER 0
#define THREAD_MAIN 1
#define THREAD_SOCKET 2
#define THREAD_TIMER 3
#define THREAD_MONITOR 4

void skynet_start(struct skynet_config * config);

#endif
