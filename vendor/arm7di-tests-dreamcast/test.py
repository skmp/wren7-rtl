import re
import os

with open('raw.s', 'r') as f:
    content = f.read()

sections = content.split('bl 	DrawResult')

testnums = { }
for i, section in enumerate(sections):
    
    lines = section.splitlines()
    for idx, line in enumerate(lines):
        if line.lstrip().startswith('@'):
            section = '\n'.join(lines[idx:])
            break
    section = '\n'.join([line for line in section.splitlines() if line.strip().strip() != ''])

    if len(section) == 0:
        continue

    first_line = section.splitlines()[0] if section.splitlines() else ''
    name_match = re.match(r'^\s*@\s*(\S+)', first_line)
    if not name_match:
        raise Exception(f"Section {i} does not start with expected pattern:\n{section}")
    name = name_match.group(1)

    last_line = section.splitlines()[-1] if section.splitlines() else ''
    match = re.match(r'^\s*ldr\s+r0,=sz(.+)$', last_line)
    if not match:
        raise Exception(f"Section {i} does not end with expected pattern:\n{section}")
    
    value = match.group(1)

    section = '\n'.join(section.splitlines()[1:-1])
    section += '\n'

    if value != name:
        name = f'{value}: {name}'

    section = f"""@ {name}\n
.equ BAD_Rd,	0x10
.equ BAD_Rn,	0x20
.equ VARBASE,	0x80000

.global start
start:
""" + section + f"""
.word 0xDEADBEEF

.align 3
var64:		.word 0x11223344,0x55667788

rdVal:		.word 0
rnVal:		.word 0
memVal:		.word 0

.align 2
exceptionFlag: .word 0

romvar:  	.byte 0x80,0,0,0
romvar2: 	.byte 0x00,0x8f,0,0xff
romvar3: 	.byte 0x80,0x7f,0,0

"""
    
    testnums.setdefault(value, 0)
    testnums[value] += 1

    with open(f'tests/{value}_{testnums[value]}.s', 'w') as out:
        out.write(section)

for filename in os.listdir('tests'):
    import subprocess
    cmd = f"{os.environ.get('DC_ARM_AS')} {os.environ.get('DC_ARM_AFLAGS')} tests/{filename} -o tmp/{filename}.o"
    result = subprocess.run(cmd, shell=True)
    if result.returncode != 0:
        raise RuntimeError(f"Command failed with exit code {result.returncode}: {cmd}")
    
    cmd = f"{os.environ.get('DC_ARM_CC')} -Wl,-Ttext,0x00000000,-N -nostartfiles -nostdlib -e start -o tmp/{filename}.elf tmp/{filename}.o"
    result = subprocess.run(cmd, shell=True)
    if result.returncode != 0:
        raise RuntimeError(f"Command failed with exit code {result.returncode}: {cmd}")

    cmd = f"{os.environ.get('DC_ARM_OBJCOPY')} -O binary tmp/{filename}.elf bins/{filename}.bin"
    result = subprocess.run(cmd, shell=True)
    if result.returncode != 0:
        raise RuntimeError(f"Command failed with exit code {result.returncode}: {cmd}")