#include "skynet.h"
#include "skynet_env.h"
#include "spinlock.h"

#include <lua.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <assert.h>

struct skynet_env {
	struct spinlock lock; // 自旋锁
	lua_State *L; // lua状态机
};

static struct skynet_env *E = NULL;

const char * 
skynet_getenv(const char *key) {
	SPIN_LOCK(E)

	lua_State *L = E->L;
	// 将全局变量key压入栈
	lua_getglobal(L, key);
	// 取出栈顶的值
	const char * result = lua_tostring(L, -1);
	// 弹出栈顶的值
	lua_pop(L, 1);

	SPIN_UNLOCK(E)

	return result;
}

void 
skynet_setenv(const char *key, const char *value) {
	SPIN_LOCK(E)
	
	lua_State *L = E->L;
	// 将全局变量key的值压栈，返回该值的类型
	lua_getglobal(L, key);
	// 确保最后压入的栈内容为空(key的值为nil)
	assert(lua_isnil(L, -1));
	// 从栈顶中弹出一个值(key的值)
	lua_pop(L,1);
	// value值压入栈
	lua_pushstring(L,value);
	// 从栈中弹出一个值，并将其赋值为全局变量key中
	lua_setglobal(L,key);

	SPIN_UNLOCK(E)
}

void
skynet_env_init() {
	// 分配内存
	E = skynet_malloc(sizeof(*E));
	// 锁初始化
	SPIN_INIT(E)
	// 创建Lua状态机
	E->L = luaL_newstate();
}
