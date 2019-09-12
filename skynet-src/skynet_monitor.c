#include "skynet.h"

#include "skynet_monitor.h"
#include "skynet_server.h"
#include "skynet.h"
#include "atomic.h"

#include <stdlib.h>
#include <string.h>

struct skynet_monitor {
	int version;
	int check_version;
	uint32_t source;
	uint32_t destination;
};

struct skynet_monitor * 
skynet_monitor_new() {
	// 分配内存
	struct skynet_monitor * ret = skynet_malloc(sizeof(*ret));
	// 置空
	memset(ret, 0, sizeof(*ret));
	// 返回
	return ret;
}

void 
skynet_monitor_delete(struct skynet_monitor *sm) {
	// 释放内存
	skynet_free(sm);
}

void 
skynet_monitor_trigger(struct skynet_monitor *sm, uint32_t source, uint32_t destination) {
	// 设置内容
	sm->source = source;
	sm->destination = destination;
	// 增加版本
	ATOM_INC(&sm->version);
}

void 
skynet_monitor_check(struct skynet_monitor *sm) {
	if (sm->version == sm->check_version) {
		if (sm->destination) {
			skynet_context_endless(sm->destination);
			skynet_error(NULL, "A message from [ :%08x ] to [ :%08x ] maybe in an endless loop (version = %d)", sm->source , sm->destination, sm->version);
		}
	} else {
		sm->check_version = sm->version;
	}
}
