#include "skynet.h"

#include "skynet_module.h"
#include "spinlock.h"

#include <assert.h>
#include <string.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_MODULE_TYPE 32

struct modules {
	int count;
	struct spinlock lock;
	const char * path;
	struct skynet_module m[MAX_MODULE_TYPE];
};

static struct modules * M = NULL;

// 读取path路径, 路径使用';'分割成一小块,使用name替换各小块中的'?',再使用dlopen读取路径为动态连接库
static void *
_try_open(struct modules *m, const char * name) {
	const char *l;
	const char * path = m->path;
	// 加载path的模块路径
	size_t path_size = strlen(path);
	// 需要加载的模块名字
	size_t name_size = strlen(name);

	// 完整路径最大长度
	int sz = path_size + name_size;
	//search path
	void * dl = NULL;
	char tmp[sz];
	do
	{
		// 重置路径
		memset(tmp,0,sz);
		// 跳过';'符号
		while (*path == ';') path++;
		// 寻找到末尾时
		if (*path == '\0') break;
		// 寻找下一个';'所在的指针位置
		l = strchr(path, ';');
		if (l == NULL) l = path + strlen(path);
		// 计算模块路径的长度
		int len = l - path;
		int i;
		// 便利路径，直到路径的末尾或者遇到'?'符
		for (i=0;path[i]!='?' && i < len ;i++) {
			tmp[i] = path[i];
		}
		// 拼接出模块的完整路径
		memcpy(tmp+i,name,name_size);
		if (path[i] == '?') {
			strncpy(tmp+i+name_size,path+i+1,len - i - 1);
		} else {
			fprintf(stderr,"Invalid C service path\n");
			exit(1);
		}
		// 加载模块路径的动态链接库，加载成功时dl非null
		dl = dlopen(tmp, RTLD_NOW | RTLD_GLOBAL);
		// 继续往后移动
		path = l;
	}while(dl == NULL);

	if (dl == NULL) {
		fprintf(stderr, "try open %s failed : %s\n",name,dlerror());
	}

	// 返回动态链接
	return dl;
}

static struct skynet_module * 
_query(const char * name) {
	int i;
	for (i=0;i<M->count;i++) {
		if (strcmp(M->m[i].name,name)==0) {
			return &M->m[i];
		}
	}
	return NULL;
}

static void *
get_api(struct skynet_module *mod, const char *api_name) {
	size_t name_size = strlen(mod->name);
	size_t api_size = strlen(api_name);
	char tmp[name_size + api_size + 1];
	memcpy(tmp, mod->name, name_size);
	memcpy(tmp+name_size, api_name, api_size+1);
	char *ptr = strrchr(tmp, '.');
	if (ptr == NULL) {
		ptr = tmp;
	} else {
		ptr = ptr + 1;
	}
	return dlsym(mod->module, ptr);
}

static int
open_sym(struct skynet_module *mod) {
	mod->create = get_api(mod, "_create");
	mod->init = get_api(mod, "_init");
	mod->release = get_api(mod, "_release");
	mod->signal = get_api(mod, "_signal");

	return mod->init == NULL;
}

struct skynet_module * 
skynet_module_query(const char * name) {
	struct skynet_module * result = _query(name);
	if (result)
		return result;

	// 加锁
	SPIN_LOCK(M)

	// 再次查询
	result = _query(name); // double check

	if (result == NULL && M->count < MAX_MODULE_TYPE) {
		int index = M->count;
		// 从path中路径中读取动态链接库
		void * dl = _try_open(M,name);
		if (dl) {
			M->m[index].name = name;
			M->m[index].module = dl;

			// 从动态链接库中提出相应的init等函数
			if (open_sym(&M->m[index]) == 0) {
				// 复制名字
				M->m[index].name = skynet_strdup(name);
				// 数量增加
				M->count ++;
				// 返回结果
				result = &M->m[index];
			}
		}
	}

	// 解锁
	SPIN_UNLOCK(M)

	return result;
}

void 
skynet_module_insert(struct skynet_module *mod) {
	// 加锁
	SPIN_LOCK(M)

	// 确保模块不存在时，添加到模块
	struct skynet_module * m = _query(mod->name);
	assert(m == NULL && M->count < MAX_MODULE_TYPE);
	int index = M->count;
	M->m[index] = *mod;
	++M->count;

	// 释放锁
	SPIN_UNLOCK(M)
}

void * 
skynet_module_instance_create(struct skynet_module *m) {
	if (m->create) {
		return m->create();
	} else {
		return (void *)(intptr_t)(~0);
	}
}

int
skynet_module_instance_init(struct skynet_module *m, void * inst, struct skynet_context *ctx, const char * parm) {
	return m->init(inst, ctx, parm);
}

void 
skynet_module_instance_release(struct skynet_module *m, void *inst) {
	if (m->release) {
		m->release(inst);
	}
}

void
skynet_module_instance_signal(struct skynet_module *m, void *inst, int signal) {
	if (m->signal) {
		m->signal(inst, signal);
	}
}

void 
skynet_module_init(const char *path) {
	// 新建变量并分配内存
	struct modules *m = skynet_malloc(sizeof(*m));
	// 初始化数量
	m->count = 0;
	// 复制路径
	m->path = skynet_strdup(path);
	// 初始化自旋锁
	SPIN_INIT(m)
	// 赋给全局变量
	M = m;
}
