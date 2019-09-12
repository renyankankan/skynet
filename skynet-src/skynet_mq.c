#include "skynet.h"
#include "skynet_mq.h"
#include "skynet_handle.h"
#include "spinlock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#define DEFAULT_QUEUE_SIZE 64
#define MAX_GLOBAL_MQ 0x10000

// 0 means mq is not in global mq.
// 1 means mq is in global mq , or the message is dispatching.

#define MQ_IN_GLOBAL 1
#define MQ_OVERLOAD 1024

// 消息队列
struct message_queue {
	struct spinlock lock; // 自旋锁
	uint32_t handle; // 出来queue的ctx的handle编号
	int cap; // 消息容量
	int head; // 队列头
	int tail; // 队列尾
	int release; 
	int in_global;
	int overload;
	int overload_threshold;
	struct skynet_message *queue; // 消息队列指针
	struct message_queue *next; // 下一个消息队列
};

struct global_queue {
	struct message_queue *head;
	struct message_queue *tail;
	struct spinlock lock;
};

static struct global_queue *Q = NULL;

// 添加消息队列到全局消息队列
void 
skynet_globalmq_push(struct message_queue * queue) {
	struct global_queue *q= Q;
	// 先加锁
	SPIN_LOCK(q)
	assert(queue->next == NULL);
	if(q->tail) {
		q->tail->next = queue;
		q->tail = queue;
	} else {
		q->head = q->tail = queue;
	}
	// 解锁
	SPIN_UNLOCK(q)
}

// 从全局消息队列中一个消息队列
struct message_queue * 
skynet_globalmq_pop() {
	struct global_queue *q = Q;

	SPIN_LOCK(q)
	struct message_queue *mq = q->head;
	if(mq) {
		q->head = mq->next;
		if(q->head == NULL) {
			assert(mq == q->tail);
			q->tail = NULL;
		}
		mq->next = NULL;
	}
	SPIN_UNLOCK(q)

	return mq;
}

// 创建一条消息队列
struct message_queue * 
skynet_mq_create(uint32_t handle) {
	// 分配内存
	struct message_queue *q = skynet_malloc(sizeof(*q));
	// 处理
	q->handle = handle;
	// 容量
	q->cap = DEFAULT_QUEUE_SIZE;
	// 头
	q->head = 0;
	// 尾巴
	q->tail = 0;
	// 锁定
	SPIN_INIT(q)
	// When the queue is create (always between service create and service init) ,
	// set in_global flag to avoid push it to global queue .
	// If the service init success, skynet_context_new will call skynet_mq_push to push it to global queue.
	q->in_global = MQ_IN_GLOBAL; // 是否添加到Q
	q->release = 0;
	q->overload = 0;
	// 过载门槛
	q->overload_threshold = MQ_OVERLOAD;
	// 分配队列内存
	q->queue = skynet_malloc(sizeof(struct skynet_message) * q->cap);
	q->next = NULL;

	return q;
}

static void 
_release(struct message_queue *q) {
	assert(q->next == NULL);
	SPIN_DESTROY(q)
	skynet_free(q->queue);
	skynet_free(q);
}

uint32_t 
skynet_mq_handle(struct message_queue *q) {
	return q->handle;
}

int
skynet_mq_length(struct message_queue *q) {
	int head, tail,cap;

	SPIN_LOCK(q)
	head = q->head;
	tail = q->tail;
	cap = q->cap;
	SPIN_UNLOCK(q)
	
	if (head <= tail) {
		return tail - head;
	}
	return tail + cap - head;
}

int
skynet_mq_overload(struct message_queue *q) {
	if (q->overload) {
		int overload = q->overload;
		q->overload = 0;
		return overload;
	} 
	return 0;
}

int
skynet_mq_pop(struct message_queue *q, struct skynet_message *message) {
	int ret = 1;
	// 加锁
	SPIN_LOCK(q)

	if (q->head != q->tail) {
		// 取出q->head位置的数据赋值给message所指的内存
		*message = q->queue[q->head++];
		ret = 0;
		int head = q->head;
		int tail = q->tail;
		int cap = q->cap;

		if (head >= cap) {
			q->head = head = 0;
		}
		int length = tail - head;
		if (length < 0) {
			length += cap;
		}
		while (length > q->overload_threshold) {
			q->overload = length;
			q->overload_threshold *= 2;
		}
	} else {
		// reset overload_threshold when queue is empty
		q->overload_threshold = MQ_OVERLOAD;
	}

	if (ret) {
		q->in_global = 0;
	}
	
	// 解锁
	SPIN_UNLOCK(q)

	return ret;
}

static void
expand_queue(struct message_queue *q) {
	// 新建原先队列两倍的内存的队列
	struct skynet_message *new_queue = skynet_malloc(sizeof(struct skynet_message) * q->cap * 2);
	int i;
	// 复制内容到新的队列
	for (i=0;i<q->cap;i++) {
		new_queue[i] = q->queue[(q->head + i) % q->cap];
	}
	q->head = 0;
	q->tail = q->cap;
	q->cap *= 2;
	
	// 释放旧的队列
	skynet_free(q->queue);
	// 设置新的队列
	q->queue = new_queue;
}

void 
skynet_mq_push(struct message_queue *q, struct skynet_message *message) {
	assert(message);
	// 加锁
	SPIN_LOCK(q)

	// 取message指针所指的内容，赋值个队列
	q->queue[q->tail] = *message;
	// tail增加或者置为0
	if (++ q->tail >= q->cap) {
		q->tail = 0;
	}

	// 如果tail==head表明容量用完,执行扩容操作
	if (q->head == q->tail) {
		expand_queue(q);
	}

	// 如果尚未添加到Q, 则添加到Q
	if (q->in_global == 0) {
		q->in_global = MQ_IN_GLOBAL;
		skynet_globalmq_push(q);
	}
	
	// 解锁
	SPIN_UNLOCK(q)
}

// 初始化全局消息队列
void 
skynet_mq_init() {
	// 分配内存
	struct global_queue *q = skynet_malloc(sizeof(*q));
	// 清空数据
	memset(q,0,sizeof(*q));
	// 初始化锁
	SPIN_INIT(q);
	// 赋值给全局消息队列
	Q=q;
}

void 
skynet_mq_mark_release(struct message_queue *q) {
	SPIN_LOCK(q)
	assert(q->release == 0);
	q->release = 1;
	if (q->in_global != MQ_IN_GLOBAL) {
		skynet_globalmq_push(q);
	}
	SPIN_UNLOCK(q)
}

static void
_drop_queue(struct message_queue *q, message_drop drop_func, void *ud) {
	struct skynet_message msg;
	while(!skynet_mq_pop(q, &msg)) {
		drop_func(&msg, ud);
	}
	_release(q);
}

void 
skynet_mq_release(struct message_queue *q, message_drop drop_func, void *ud) {
	SPIN_LOCK(q)
	
	if (q->release) {
		SPIN_UNLOCK(q)
		_drop_queue(q, drop_func, ud);
	} else {
		skynet_globalmq_push(q);
		SPIN_UNLOCK(q)
	}
}
