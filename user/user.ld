/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */

OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64",
              "elf64-x86-64")

ENTRY(_start)

SECTIONS
{

	. = 0x400000;

	.text : { *(.text) }


	/* skip to the next page */
  	. = ALIGN(CONSTANT (MAXPAGESIZE));

	/*
	 * .bss not being last means it doesn't make filesz < memsz
	 * and that my crappy elf loader can work :)
	 * The downside is obviously that .bss needs to exist on the filesystem...
	 *
	 * TODO: make a better elf loader :)
	 */
	.bss : { *(.bss) }
	.data : { *(.data) }
}


