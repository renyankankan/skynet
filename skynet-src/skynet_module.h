#ifndef SKYNET_MODULE_H
#define SKYNET_MODULE_H

struct skynet_context;

typedef void * (*skynet_dl_create)(void);
typedef int (*skynet_dl_init)(void * inst, struct skynet_context *, const char * parm);
typedef void (*skynet_dl_release)(void * inst);
typedef void (*skynet_dl_signal)(void * inst, int signal);

struct skynet_module {
	// 模块名字
	const char * name;
	// 模块动态链接库内容
	void * module;
	// 动态链接库的create方法
	skynet_dl_create create;
	// 动态链接库的init方法
	skynet_dl_init init;
	// 动态链接库的release方法
	skynet_dl_release release;
	// 动态链接库的signal方法
	skynet_dl_signal signal;
};

// 插入模块
void skynet_module_insert(struct skynet_module *mod);
// 查询模块
struct skynet_module * skynet_module_query(const char * name);

// 创建模块实例
void * skynet_module_instance_create(struct skynet_module *);
// 初始化模块实例
int skynet_module_instance_init(struct skynet_module *, void * inst, struct skynet_context *ctx, const char * parm);
// 模块实例释放
void skynet_module_instance_release(struct skynet_module *, void *inst);
// 模块实例处理信号
void skynet_module_instance_signal(struct skynet_module *, void *inst, int signal);

// 初始化模块
void skynet_module_init(const char *path);

#endif
