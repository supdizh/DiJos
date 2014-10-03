// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/pmap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/env.h>
#include <kern/cpu.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display Stack backtrace", mon_backtrace },
	{ "showmappings", "show Virtual to Physical mapping", mon_showmappings},
	{ "setmapping_perm", "set permission",mon_setmapping_perm},
	{ "dumpmem", "dump memory",mon_dumpmem},
	{ "continue", "Continue exec from breakpoint",mon_continue},
	{"si","Single-step one instruction from breakpoint",mon_si},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))


/**helper**/
const char* perm_str(int perm){
	if(perm & PTE_U){
		if(perm & PTE_W)
			return "user:W/R";
		else "user:RO";
	}else{
		return "kern:W/R";
	}
	return "error";
}


/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t *ebp, *eip;
	uint32_t arg0, arg1, arg2, arg3, arg4;
	struct Eipdebuginfo info;

	ebp = (uint32_t*) read_ebp();//read_ebp return an uint32_t

	cprintf("Stack backtrace:\n");
	while(ebp != 0){
		eip = (uint32_t*) ebp[1];//of the new ebp
		arg0 = ebp[2];
		arg1 = ebp[3];
		arg2 = ebp[4];
		arg3 = ebp[5];
		arg4 = ebp[6];
		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
			       ebp, eip, arg0, arg1, arg2, arg3, arg4);
		debuginfo_eip((uint32_t)eip,&info);
		cprintf("         %s:%d: %.*s+%d\n",info.eip_file,info.eip_line,info.eip_fn_namelen,info.eip_fn_name,(uint32_t)eip-info.eip_fn_addr);

		ebp = (uint32_t*) ebp[0];


	}

	return 0;
}

int
mon_showmappings(int argc,char **argv, struct Trapframe *tf){
	if(argc != 3){
		cprintf("Usage: showmappings [VA from] [Va to]\n");
		return 0;
	}
	uint32_t lva = strtol (argv[1], 0, 0);
	uint32_t hva = strtol (argv[2], 0, 0);
	if(lva > hva){
		cprintf("showmappings: Invalid address from > to\n");
		return 0;
	}
	lva = ROUNDDOWN(lva, PGSIZE);
	pte_t *pte;

	while(lva < hva){
		pte = pgdir_walk(kern_pgdir, (void*)lva, 0);
		cprintf("0x%x - 0x%x  ......  ",lva, lva + PGSIZE);
		if(NULL == pte || !(*pte & PTE_P)){
			cprintf("not mapped\n");
		}else{
			cprintf("0x%x  ",PTE_ADDR(*pte));
			cprintf("%s",perm_str(*pte));
			cprintf("\n");
		}
		lva += PGSIZE;
	}
	return 0;
}

int
mon_setmapping_perm(int argc,char **argv, struct Trapframe *tf){
	if(argc != 4){
		cprintf("Usage: setmapping_perm [VA from] [Va to] ['UW'] \n");
		return 0;
	}
	uint32_t lva = strtol (argv[1], 0, 0);
	uint32_t hva = strtol (argv[2], 0, 0);
	if(lva > hva){
		cprintf("setmapping_perm: Invalid address from > to\n");
		return 0;
	}
	lva = ROUNDDOWN(lva, PGSIZE);
	int perm;
	pte_t *pte;
	char *p = argv[3];
	while(*p != '\0'){
		if(*p == 'U')perm |= PTE_U;
		else if(*p == 'W')perm |= PTE_W;
		p++;
	}

	while(lva < hva){
		pte = pgdir_walk(kern_pgdir, (void*)lva, 0);
		cprintf("0x%x - 0x%x  ......  ",lva, lva + PGSIZE);
		if(NULL == pte || !(*pte & PTE_P)){
			cprintf("not mapped\n");
		}else{
			cprintf("0x%x  ",PTE_ADDR(*pte));
			cprintf("%s",perm_str(*pte));
			cprintf(" ----> %s", perm_str(perm));
			cprintf("\n");
			*pte = (*pte&(~0xfff))|perm|PTE_P;
		}
		lva += PGSIZE;
	}
	return 0;
}

int
mon_dumpmem(int argc,char **argv, struct Trapframe *tf){
	if(argc != 4){
		cprintf("Usage: dumpmem [p/v] [VA from] [n 4byte-words]\n");
		return 0;
	}
	uint32_t lva = strtol(argv[2], 0, 0);
	uint32_t hva  = strtol(argv[3], 0, 10) * 4 + lva;
	if(argv[1][0] != 'v' && argv[1][0] !='p'){
		cprintf("dumpmem adress type p or v\n");
		return 0;
	}

	//here a fessible solution because 256MB mapped for KERNBASE->PHYTOP
	//and whole mem for JOS is 64MB,which means, all mapped above KERNBASE
	if(argv[1][0] == 'p'){
		lva += KERNBASE;
		hva += KERNBASE;
	}

	int i;
	while(lva < hva){
		cprintf("0x%x:  ",lva);
		for(i=0;i<4 &&lva <hva;i++,lva += 4){
			cprintf("0x%08x\t",*((uint32_t*)lva));
		}
		cprintf("\n");
	}
	return 0;
}

int mon_continue(int argc, char **argv,struct Trapframe *tf){
	if(tf == NULL){
		cprintf("Cannot continue: Trapframe == NULL\n");
		return -1;
	}
	else if(tf->tf_trapno != T_BRKPT && tf->tf_trapno != T_DEBUG){
		cprintf("Cannot continue: trapno!=T_BRKPT|T_DEBUG\n");
		return -1;
	}
	cprintf("Continue exec from breakpoint...\n");
	tf->tf_eflags &= ~FL_TF;
	env_run(curenv);
	return 0;
}

int mon_si(int argc, char **argv, struct Trapframe *tf){
	if(tf == NULL){
		cprintf("Cannot single-step: tf==NULL\n");
		return -1;
	}
	else if(tf->tf_trapno != T_BRKPT && tf->tf_trapno != T_DEBUG){
		cprintf("Cannot continue: wrong trap num\n");
		return -1;
	}
	cprintf("Single-step one instruction at a time...");
	tf->tf_eflags |= FL_TF;
	struct Eipdebuginfo info;
	debuginfo_eip(tf->tf_eip, &info);
	cprintf("\"Si\" information:\ntf_eip=%08x\n%s:%d: %.*s+%d\n",
	tf->tf_eip, info.eip_file, info.eip_line,
	info.eip_fn_namelen, info.eip_fn_name, tf->tf_eip-info.eip_fn_addr);
	env_run(curenv);
	return 0;
}
/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("%CredWelcome to %Corgthe %CcynJOS kernel %Cwhtmonitor!\n");	
	//
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
