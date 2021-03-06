#include <stdlib.h>
#include "kernel.h"
#include "list.h"

#ifndef NULL
#define NULL 0
#endif


/*****************************************************************************
 * Global variables
 *****************************************************************************/

static uint32_t id=1;
Task * tsk_running = NULL;	/* pointer to ready task list : the first
                                     node is the running task descriptor */
Task * tsk_prev = NULL;
Task * tsk_sleeping = NULL;	/* pointer to sleeping task list */

/*****************************************************************************
 * SVC dispatch
 *****************************************************************************/
/* sys_add
 *   test function
 */
int sys_add(int a, int b)
{
    return a+b;
}

/* syscall_dispatch
 *   dispatch syscalls
 *   n      : syscall number
 *   args[] : array of the parameters (4 max)
 *   Fonction qui sert à repartir les appels systèmes
 */
int32_t svc_dispatch(uint32_t n, uint32_t args[])
{
    switch(n) {
      case 0:
          return sys_add((int)args[0], (int)args[1]);
      case 1:
    	  return (int32_t) malloc(args[0]);
      case 2:
    	  free((void*)args[0]);
    	  return 0;
      case 3:
    	  return sys_os_start();
      case 4:
    	  return sys_task_new((TaskCode)args[0], args[1]);
      case 5:
    	  return sys_task_id();
      case 6:
    	  return sys_task_wait(args[0]);
      case 7:
    	  return sys_task_kill();
      case 8:
    	  return (int32_t)sys_sem_new((int32_t)args[0]);
      case 9:
    	  return sys_sem_p((Semaphore *)args[0]);
      case 10:
    	  return sys_sem_v((Semaphore *)args[0]);
    }
    return -1;
}

void sys_switch_ctx()
{
	SCB->ICSR |= 1<<28; // set PendSV to pending
}
/*****************************************************************************
 * Round robin algorithm
 *****************************************************************************/
#define SYS_TICK  10	// system tick in ms

uint32_t sys_tick_cnt=0;

/* tick_cb
 *   system tick callback: task switching, ...
 */
void sys_tick_cb()
{
	   Task *t;

	    tsk_prev = tsk_running;
	    tsk_running -> status = TASK_READY;

	    int lsize = list_size(tsk_sleeping);


	    while( tsk_sleeping && lsize)
	    {
	        tsk_sleeping -> delay -= SYS_TICK;
	        if(tsk_sleeping-> delay <=0)
	        {
	            tsk_sleeping = list_remove_head(tsk_sleeping,&t);
	            tsk_running = list_insert_tail(tsk_running,t);
	            t -> status = TASK_READY;
	        }
	        if(tsk_sleeping) tsk_sleeping = tsk_sleeping -> next;
	        lsize--;
	    }



	    tsk_running = tsk_running->next;
	    tsk_running -> status = TASK_RUNNING;

	    sys_switch_ctx();


	//    list_display(tsk_running);
}

void SysTick_Handler(void)
{
	sys_tick_cnt++;

	if (sys_tick_cnt == SYS_TICK) {
		sys_tick_cnt = 0;
		sys_tick_cb();
	}
}

/*****************************************************************************
 * General OS handling functions
 *****************************************************************************/

/* sys_os_start
 *   start the first created task
 */
int32_t sys_os_start()
{
	tsk_running->status = TASK_RUNNING;
	sys_switch_ctx();

    // Reset BASEPRI
    __set_BASEPRI(0);

	// Set systick reload value to generate 1ms interrupt
    SysTick_Config(SystemCoreClock / 1000U);
    return 0;
}

/*****************************************************************************
 * Task handling functions
 *****************************************************************************/
void task_kill();

/* sys_task_new
 *   create a new task :
 *   func      : task code to be run
 *   stacksize : task stack size
 *
 *   Stack frame:
 *      |    xPSR    |
 *      |     PC     |
 *      |     LR     |
 *      |     R12    |    ^
 *      |     R3     |    ^
 *      |     R2     |    | @++
 *      |     R1     |
 *      |     R0     |
 *      +------------+
 *      |     R11    |
 *      |     R10    |
 *      |     R9     |
 *      |     R8     |
 *      |     R7     |
 *      |     R6     |
 *      |     R5     |
 *      |     R4     |
 *      +------------+
 *      | EXC_RETURN |
 *      |   CONTROL  | <- sp
 *      +------------+
 */
int32_t sys_task_new(TaskCode func, uint32_t stacksize)
{
	// get a stack with size multiple of 8 bytes (cf. page 13)
		uint32_t size = stacksize > 96 ? 8 * (((stacksize - 1) / 8) + 1) : 96;

		Task *t = (Task *)malloc(sizeof(Task) + size);

		if (t)
		{
			t->id = id++;					// identifier
			t->status = TASK_READY;			// task status : running, ready, ...
			t->delay = 0;		   // waiting delay (for timeouts)

			t-> splim = (uint32_t*)(t+1);  // bottom stack limit
			t-> sp = t->splim + (size>>2); // top stack
			t-> sp =  (t-> sp) -18; // push context

			// save stack frame
			t->sp[17] = 0x01000000;  // thumb mode bit   // SR
			t->sp[16] = (uint32_t)func;  // PC
			t->sp[15]= (uint32_t)task_kill;  // LR
			t->sp[1] = 0xFFFFFFFD; // EXC_RETURN code to return in thread mode
			t->sp[0] = 0x1;		   // CONTROL thread mode privileged level => unprivileged

			tsk_running = list_insert_tail(tsk_running, t);
			return t->id;
		}
		return -1;
}

/* sys_task_kill
 *   kill oneself
 */
int32_t sys_task_kill()
{
    Task* t;
    tsk_running = list_remove_head(tsk_running, &t);
    tsk_prev=NULL;
    free(t);
    tsk_running -> status = TASK_RUNNING;
    sys_switch_ctx();

    return 0;
}


/* sys_task_id
 *   returns id of task
 */
int32_t sys_task_id()
{
	if(tsk_running)
	{
		return tsk_running->id;
	}
	else
	{
		return -1;
	}
}


/* sys_task_yield
 *   run scheduler to switch to another task
 */
int32_t sys_task_yield()
{

    return -1;
}

/* task_wait
 *   suspend the current task until timeout
 */
int32_t sys_task_wait(uint32_t ms)
{
    Task *t;
    tsk_running ->  status =  TASK_SLEEPING;
    tsk_prev = tsk_running;
    tsk_running -> delay = ms;

    tsk_running = list_remove_head(tsk_running,&t);
    tsk_sleeping=list_insert_head(tsk_sleeping,t);

    tsk_running-> status=TASK_RUNNING;
    sys_switch_ctx();

    return 0;
}


/*****************************************************************************
 * Semaphore handling functions
 *****************************************************************************/

/* sys_sem_new
 *   create a semaphore
 *   init    : initial value
 */
Semaphore * sys_sem_new(int32_t init)
{
	Semaphore *sem=(Semaphore *)malloc(sizeof(Semaphore));

	if(sem)
	{
	    sem -> count = init ;
	    sem -> waiting = NULL;
	    return sem;

	}

	return sem;
}

/* sys_sem_p
 *   take a token
 */
int32_t sys_sem_p(Semaphore * sem)
{
	Task *t;
	if ( sem )
	{

	    sem -> count--;

	    if(sem -> count < 0)
	    {
	        tsk_running -> status = TASK_WAITING;
	        tsk_prev = tsk_running;

	        tsk_running = list_remove_head( tsk_running, &t );
	        sem -> waiting = list_insert_tail( sem -> waiting, t);

	        tsk_running -> status = TASK_RUNNING;
	        sys_switch_ctx();
	    }
	    return 0;
	}
	return -1;
}

/* sys_sem_v
 *   release a token
 */
int32_t sys_sem_v(Semaphore * sem)
{
    Task *t;
    if(sem)
    {
        sem -> count++;
        if(sem -> waiting)
        {
            tsk_running -> status = TASK_READY;
            tsk_prev = tsk_running;
            sem -> waiting = list_remove_head( sem -> waiting, &t);
            tsk_running = list_insert_head(tsk_running, t);
            tsk_running -> status = TASK_RUNNING;
            sys_switch_ctx();
        }
        return 0;
    }
    return -1;
}
