#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/spinlock.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

void sched_halt(void);

// Choose a user environment to run and run it.
void
sched_yield(void)
{
	struct Env *idle;
	assert(!(read_eflags() & FL_IF));
	//if(curenv)
		//cprintf("sched called from %08x @0x%08x \n",curenv->env_id,curenv);
	// Implement simple round-robin scheduling.
	//
	// Search through 'envs' for an ENV_RUNNABLE environment in
	// circular fashion starting just after the env this CPU was
	// last running.  Switch to the first such environment found.
	//
	// If no envs are runnable, but the environment previously
	// running on this CPU is still ENV_RUNNING, it's okay to
	// choose that environment.
	//
	// Never choose an environment that's currently running on
	// another CPU (env_status == ENV_RUNNING). If there are
	// no runnable environments, simply drop through to the code
	// below to halt the cpu.

	// LAB 4: Your code here
	int envCnt = 0;
	idle = curenv;
	if(!curenv)
		idle = envs + NENV - 1; //ugly set it to start from the envs
	for(idle ++; envCnt < NENV-1; envCnt++, idle++){
		//cprintf("rolling idle@0x%08x,[%08x] %d \n",idle,idle->env_id,idle->env_status);
		if(idle >= envs+NENV)
			idle = envs;
		if(idle->env_status == ENV_RUNNABLE){
			//cprintf("run idleid = %08x\n",idle->env_id);
			env_run(idle);//do not return
		}
	}
	if(idle >= envs+NENV)
	idle = envs;
	//cprintf("last idle@0x%08x,[%08x] %d \n",idle,idle->env_id,idle->env_status);
	//cprintf("sizeof ENV:%d, envCnt:%d, NENV:%d",sizeof(struct Env),envCnt,NENV);
	if(idle->env_status == ENV_RUNNING){ //cannot use curenv,because curenv could be NULL
		//cprintf("juz before env_run idle id = %08x\n",idle->env_id);
		env_run(idle);
	}
	// sched_halt never returns
	//cprintf("no env to run,call halt\n");
	sched_halt();
}

// Halt this CPU when there is nothing to do. Wait until the
// timer interrupt wakes it up. This function never returns.
//
void
sched_halt(void)
{
	int i;

	// For debugging and testing purposes, if there are no runnable
	// environments in the system, then drop into the kernel monitor.
	for (i = 0; i < NENV; i++) {
		if ((envs[i].env_status == ENV_RUNNABLE ||
		     envs[i].env_status == ENV_RUNNING ||
		     envs[i].env_status == ENV_DYING))
			break;
	}
	if (i == NENV) {
		cprintf("No runnable environments in the system!\n");
		while (1)
			monitor(NULL); //we should have at least one cpu running terminals
	}

	// Mark that no environment is running on this CPU
	curenv = NULL;
	lcr3(PADDR(kern_pgdir));

	// Mark that this CPU is in the HALT state, so that when
	// timer interupts come in, we know we should re-acquire the
	// big kernel lock
	xchg(&thiscpu->cpu_status, CPU_HALTED);

	// Release the big kernel lock as if we were "leaving" the kernel
	unlock_kernel();

	// Reset stack pointer, enable interrupts and then halt.
	asm volatile (
		"movl $0, %%ebp\n"
		"movl %0, %%esp\n"
		"pushl $0\n"
		"pushl $0\n"
		"sti\n"
		"hlt\n"
	: : "a" (thiscpu->cpu_ts.ts_esp0));
}

