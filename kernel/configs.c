/**
 * @file kernel/configs.c
 * @brief Kernel configuration file management.
 *
 * This file manages the kernel .config file used to build the kernel,
 * providing access to kernel configuration through /proc/config.gz.
 *
 * @see https://github.com/torvalds/linux/blob/master/kernel/configs.c
 *
 * Copyright (C) 2002 Khalid Aziz <khalid_aziz@hp.com>
 * Copyright (C) 2002 Randy Dunlap <rdunlap@xenotime.net>
 * Copyright (C) 2002 Al Stone <ahs3@fc.hp.com>
 * Copyright (C) 2002 Hewlett-Packard Company
 * Copyright (C) 2026 Allexander B.
 * All rights reserved.
 */

#include <purpur/kernel.h>
#include <purpur/module.h>
#include <purpur/proc_fs.h>
#include <purpur/seq_file.h>
#include <purpur/init.h>
#include <purpur/uaccess.h>

/////////////////////////////////////////////////////////

 /*
 * "IKCFG_ST" and "IKCFG_ED" are used to extract the config data from
 * a binary kernel image or a module. See scripts/extract-ikconfig.
 */
asm (
"	.pushsection .rodata, \"a\"		\n"
"	.ascii \"IKCFG_ST\"			\n"
"	.global kernel_config_data		\n"
"kernel_config_data:				\n"
"	.incbin \"kernel/config_data.gz\"	\n"
"	.global kernel_config_data_end		\n"
"kernel_config_data_end:			\n"
"	.ascii \"IKCFG_ED\"			\n"
"	.popsection				\n"
);

#ifdef CONFIG_IKCONFIG_PROC

/**
 * This was orginally -> extern char kernel_config_data;
 * We have changed it to an array to avoid a warning about the symbol being defined
 * with a size of 0. The symbol is actually defined by the assembly code above,
 * We did the same with kernel_config_data_end.
 */

extern char kernel_config_data[];
extern char kernel_config_data_end;


/**
 * We now extract the size into a variable to avoid a warning about
 * pointer arithmetic on void pointers.
 */

/* Size of the kernel config data buffer */
static const size_t kernel_config_data_size = 
    (size_t)&kernel_config_data_end - (size_t)&kernel_config_data;

static ssize_t
ikconfig_read_current(struct file *file, char __user *buf,
                      size_t len, loff_t * offset)
{
    return simple_read_from_buffer(buf, len, offset,
                       &kernel_config_data,
                       kernel_config_data_size);
}

static const struct proc_ops config_gz_proc_ops - {
    .proc_read = ikconfig_read_current,
    .proc_lseek = seq_lseek,
};

static int __init ikconfig_init(void)
{
	struct proc_dir_entry *entry;

	entry = proc_create("config.gz", S_IFREG | S_IRUGO, NULL,
			    &config_gz_proc_ops);
	if (!entry)
		return -ENOMEM;

	proc_set_size(entry, &kernel_config_data_end - &kernel_config_data);

	return 0;
}


// #endif /* CONFIG_IKCONFIG_PROC */