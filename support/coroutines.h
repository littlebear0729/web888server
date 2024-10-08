//////////////////////////////////////////////////////////////////////////
// Homemade GPS Receiver
// Copyright (C) 2013 Andrew Holme
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// http://www.holmea.demon.co.uk/GPS/Main.htm
//////////////////////////////////////////////////////////////////////////

// Copyright (c) 2014-2016 John Seamons, ZL/KF6VO

#ifndef _COROUTINES_H_
#define _COROUTINES_H_

#include "types.h"
#include "config.h"
#include "timing.h"

#include <pthread.h>
#include <stdint.h>

#define HIGHEST_PRIORITY 99

#define DATAPUMP_PRIORITY 80

#define SND_PRIORITY 70
#define WF_PRIORITY  60

#define TASK_MED_PRIORITY 50

#define EXT_PRIORITY       50
#define WEBSERVER_PRIORITY 50

#define ADMIN_PRIORITY    40
#define GPS_PRIORITY      30
#define EXT_PRIORITY_LOW  30
#define GPS_ACQ_PRIORITY  30
#define SERVICES_PRIORITY 30
#define MAIN_PRIORITY     30
#define CAT_PRIORITY      20

#define LOWEST_PRIORITY 1
#define NUM_PRIORITY    (HIGHEST_PRIORITY)

typedef int tid_t;
#define TID_MAIN 0

void TaskInit();
void TaskInitCfg();
void TaskCollect();

#define CTF_CHANNEL        (MAX_RX_CHANS - 1)
#define CTF_RX_CHANNEL     0x0010
#define CTF_BUSY_HELPER    0x0020
#define CTF_FORK_CHILD     0x0080
#define CTF_PRIO_INVERSION 0x0100
#define CTF_NO_CHARGE      0x0200
#define CTF_TNAME_FREE     0x0400
#define CTF_NO_PRIO_INV    0x0800

#define CTF_STACK_SIZE  0x3000
#define CTF_STACK_REG   0x0000
#define CTF_STACK_MED   0x1000
#define CTF_STACK_LARGE 0x2000

#define CTF_SOFT_FAIL 0x4000
#define CTF_NO_LOG    0x8000

C_LINKAGE(int _CreateTask(funcP_t entry, const char* name, void* param, int priority, u4_t flags, int f_arg));
#define CreateTask(f, param, priority)                 _CreateTask(f, #f, param, priority, 0, 0)
#define CreateTaskF(f, param, priority, flags)         _CreateTask(f, #f, param, priority, flags, 0)
#define CreateTaskFA(f, param, priority, flags, fa)    _CreateTask(f, #f, param, priority, flags, fa)
#define CreateTaskSF(f, s, param, priority, flags, fa) _CreateTask(f, s, param, priority, flags, fa)

// usec == 0 means sleep until someone does TaskWakeup() on us
// usec > 0 is microseconds time in future (added to current time)
C_LINKAGE(void* _TaskSleep(const char* reason, u64_t usec, u4_t* wakeup_test));
#define TaskSleep()                _TaskSleep("TaskSleep", 0, NULL)
#define TaskSleepUsec(us)          _TaskSleep("TaskSleep", us, NULL)
#define TaskSleepMsec(ms)          _TaskSleep("TaskSleep", MSEC_TO_USEC(ms), NULL)
#define TaskSleepSec(s)            _TaskSleep("TaskSleep", SEC_TO_USEC(s), NULL)
#define TaskSleepReason(r)         _TaskSleep(r, 0, NULL)
#define TaskSleepReasonUsec(r, us) _TaskSleep(r, us, NULL)
#define TaskSleepReasonMsec(r, ms) _TaskSleep(r, MSEC_TO_USEC(ms), NULL)
#define TaskSleepReasonSec(r, s)   _TaskSleep(r, SEC_TO_USEC(s), NULL)
#define TaskSleepWakeupTest(r, wu) _TaskSleep(r, 0, wu)

#define TWF_NONE            0x0000
#define TWF_CHECK_WAKING    0x0001
#define TWF_CANCEL_DEADLINE 0x0002

C_LINKAGE(void _TaskWakeup(int id, u4_t flags, void* wake_param));
#define TaskWakeup(id)         _TaskWakeup(id, TWF_NONE, 0);
#define TaskWakeupF(id, f)     _TaskWakeup(id, f, 0);
#define TaskWakeupFP(id, f, p) _TaskWakeup(id, f, p);

typedef enum {
    CALLED_FROM_INIT,
    CALLED_WITHIN_NEXTTASK,
    CALLED_FROM_LOCK,
} ipoll_from_e;

C_LINKAGE(void TaskRemove(int id));
void TaskMinRun(u4_t minrun_us);
u4_t TaskFlags();
void TaskSetFlags(u4_t flags);
u4_t TaskPriority(int priority);
void TaskCheckStacks(bool report);
u64_t TaskStartTime();

C_LINKAGE(u4_t TaskID());
C_LINKAGE(void* TaskGetUserParam());
C_LINKAGE(void TaskSetUserParam(void* param));

// don't collide with PRINTF_FLAGS
#define TDUMP_PRINTF   0x00ff
#define TDUMP_REG      0x0000
#define TDUMP_LOG      0x0100 // shorter lines suitable for /dump URL
#define TDUMP_HIST     0x0200 // include runtime histogram
#define TDUMP_CLR_HIST 0x0400 // clear runtime histogram
void TaskDump(u4_t flags);

const char* _TaskName(const char* name, bool free_name);
#define TaskName()          _TaskName(NULL, false)
#define TaskNameS(name)     _TaskName(name, false)
#define TaskNameSFree(name) _TaskName(name, true)

C_LINKAGE(const char* Task_s(int id));
C_LINKAGE(const char* Task_ls(int id));

#define TSTAT_MASK 0x00ff
#define TSTAT_NC   0
#define TSTAT_SET  1
#define TSTAT_INCR 2
#define TSTAT_MIN  3
#define TSTAT_MAX  4

#define TSTAT_LATCH 0x0f00
#define TSTAT_ZERO  0x0100

int TaskStat(u4_t s1_func, int s1_val, const char* s1_units, u4_t s2_func DEF_0, int s2_val DEF_0, const char* s2_units DEF_NULL);
#define TaskStat2(f, v, u) TaskStat(0, 0, NULL, f, v, u);

#define NT_NONE       0
#define NT_BUSY_WAIT  1
#define NT_LONG_RUN   2
#define NT_FAST_CHECK 3

// typedef uint64_t u_int64_t;
// C_LINKAGE(void _NextTask(const char *s, u4_t param, u_int64_t pc));
#define NextTaskW(s, p)
#define NextTask(s)
#define NextTaskP(s, p)
#define NextTaskFast(s)

#define LOCK_MAGIC_B 0x10ccbbbb
#define LOCK_MAGIC_E 0x10cceeee

typedef struct
{
    u4_t magic_b;
    const char* name;
    pthread_mutex_t mutex;
#define LEN_ENTER_NAME 32
    char enter_name[LEN_ENTER_NAME];
    u4_t magic_e;
} lock_t;

#define lock_init(lock)           _lock_init(lock, #lock, false)
#define lock_init_recursive(lock) _lock_init(lock, #lock, true)
#define lock_initS(lock, name)    _lock_init(lock, name, false)

C_LINKAGE(void _lock_init(lock_t* lock, const char* name, bool recurisve));
C_LINKAGE(void lock_dump());
C_LINKAGE(bool lock_check());
C_LINKAGE(void lock_enter(lock_t* lock));
C_LINKAGE(void lock_leave(lock_t* lock));

#ifdef __cplusplus
class lock_holder {
private:
    lock_t& _owner;

public:
    lock_holder(lock_t& lock) : _owner(lock) { lock_enter(&lock); }
    ~lock_holder() { lock_leave(&_owner); }
};
#endif

#endif
