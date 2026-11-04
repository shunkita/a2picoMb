#!/usr/bin/env python3
"""
tools/inspect_critical_path.py
Core 1 Apple II Bus Read Critical Path Cycle Analyzer for a2picoMb (RP2350 / Cortex-M33)

Analyzes the instruction sequence and Worst-Case Execution Time (WCET) between:
- Start point: mb_getaddr RX FIFO read (pio0->rxf[SM_ADDR])
- End point  : a2pico_putdata TX FIFO write (pio0->txf[SM_DATA])

Supports both:
- Inlined dispatch (10-26 version and current HEAD)
- Function call dispatch (10-17 version via bl <via6522_read>)

Author: Antigravity Assistant & Shunichi Kitahara
License: MIT
"""

import sys
import os
import re
import argparse
import subprocess
import json

REG_NAMES = [
    "REG_ORB ($00)", "REG_ORA ($01)", "REG_DDRB ($02)", "REG_DDRA ($03)",
    "REG_T1CL ($04)", "REG_T1CH ($05)", "REG_T1LL ($06)", "REG_T1LH ($07)",
    "REG_T2CL ($08)", "REG_T2CH ($09)", "REG_SR ($0A)", "REG_ACR ($0B)",
    "REG_PCR ($0C)", "REG_IFR ($0D)", "REG_IER ($0E)", "REG_ORA_NH ($0F)"
]

def find_objdump():
    for cmd in ["arm-none-eabi-objdump", "llvm-objdump", "objdump"]:
        try:
            res = subprocess.run(["which", cmd], capture_output=True, text=True)
            if res.returncode == 0 and res.stdout.strip():
                return res.stdout.strip()
        except Exception:
            pass
    return None

def disassemble_functions(target_file, objdump_cmd):
    # Disassemble board and optionally via6522_read
    cmd1 = [objdump_cmd, "--disassemble=board", target_file]
    res1 = subprocess.run(cmd1, capture_output=True, text=True)
    
    cmd2 = [objdump_cmd, "--disassemble=via6522_read", target_file]
    res2 = subprocess.run(cmd2, capture_output=True, text=True)
    
    if res1.returncode != 0:
        # Fallback to full disassembly if --disassemble=<func> is not supported
        cmd = [objdump_cmd, "-d", target_file]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"objdump failed: {res.stderr}")
        return res.stdout.splitlines()

    combined = res1.stdout.splitlines()
    if res2.returncode == 0 and res2.stdout.strip():
        combined += res2.stdout.splitlines()
    return combined

def parse_disassembly(lines):
    instr_re = re.compile(r'^\s*([0-9a-fA-F]+):\s+([0-9a-fA-F ]+)\s+([a-zA-Z0-9_\.]+)(?:\s+(.*))?$')
    instructions = {}
    ordered_addrs = []

    for line in lines:
        m = instr_re.match(line)
        if m:
            addr_str, hex_bytes, mnemonic, operands = m.groups()
            addr = int(addr_str, 16)
            operands = operands.strip() if operands else ""
            clean_operands = re.sub(r'\s*[@;].*$', '', operands).strip()
            instructions[addr] = {
                'addr': addr,
                'hex': hex_bytes.strip(),
                'mnemonic': mnemonic,
                'operands': operands,
                'clean_operands': clean_operands,
                'raw': line.strip()
            }
            ordered_addrs.append(addr)

    return instructions, ordered_addrs

def parse_tbh_table(instructions, ordered_addrs, tbh_addr):
    # Cortex-M33 TBH table (halfword entries)
    pc_val = tbh_addr + 4
    idx = ordered_addrs.index(tbh_addr) + 1
    halfwords = []

    while len(halfwords) < 15 and idx < len(ordered_addrs):
        inst = instructions[ordered_addrs[idx]]
        if inst['mnemonic'] == '.word':
            val = int(inst['clean_operands'], 16)
            halfwords.append(val & 0xFFFF)
            if len(halfwords) < 15:
                halfwords.append((val >> 16) & 0xFFFF)
        elif inst['mnemonic'] == '.short':
            val = int(inst['clean_operands'], 16)
            halfwords.append(val & 0xFFFF)
        else:
            hex_clean = inst['hex'].replace(' ', '')
            if len(hex_clean) == 8:
                w_val = int(hex_clean, 16)
                halfwords.append(w_val & 0xFFFF)
                if len(halfwords) < 15:
                    halfwords.append((w_val >> 16) & 0xFFFF)
            elif len(hex_clean) == 4:
                s_val = int(hex_clean, 16)
                halfwords.append(s_val & 0xFFFF)
        idx += 1

    targets = {}
    for r_idx, hw in enumerate(halfwords):
        target_addr = pc_val + 2 * hw
        targets[r_idx + 1] = target_addr

    return targets

def parse_tbb_table(instructions, ordered_addrs, tbb_addr):
    # Cortex-M33 TBB table (byte entries)
    pc_val = tbb_addr + 4
    idx = ordered_addrs.index(tbb_addr) + 1
    bytes_list = []

    while len(bytes_list) < 15 and idx < len(ordered_addrs):
        inst = instructions[ordered_addrs[idx]]
        if inst['mnemonic'] == '.byte':
            val = int(inst['clean_operands'], 16)
            bytes_list.append(val & 0xFF)
        elif inst['mnemonic'] == '.short':
            val = int(inst['clean_operands'], 16)
            bytes_list.append(val & 0xFF)
            if len(bytes_list) < 15:
                bytes_list.append((val >> 8) & 0xFF)
        elif inst['mnemonic'] == '.word':
            val = int(inst['clean_operands'], 16)
            for shift in [0, 8, 16, 24]:
                if len(bytes_list) < 15:
                    bytes_list.append((val >> shift) & 0xFF)
        else:
            hex_clean = inst['hex'].replace(' ', '')
            for b_idx in range(0, len(hex_clean), 2):
                if len(bytes_list) < 15:
                    b_val = int(hex_clean[b_idx:b_idx + 2], 16)
                    bytes_list.append(b_val)
        idx += 1

    targets = {}
    for r_idx, b in enumerate(bytes_list):
        target_addr = pc_val + 2 * b
        targets[r_idx + 1] = target_addr

    return targets

def trace_to_putdata(instructions, ordered_addrs, start_target):
    idx = ordered_addrs.index(start_target)
    path = []
    while idx < len(ordered_addrs):
        inst = instructions[ordered_addrs[idx]]
        path.append(inst)
        if inst['mnemonic'] in ['str', 'str.w'] and '#20' in inst['clean_operands']:
            break
        if inst['mnemonic'] in ['b', 'b.n', 'b.w']:
            target_addr = int(inst['clean_operands'].split()[0], 16)
            idx = ordered_addrs.index(target_addr)
            continue
        idx += 1
    return path

def calc_inst_cycles(inst, context):
    """
    Cortex-M33 cycle estimation model based on ARM Cortex-M33 TRM.
    Returns (nominal_cycles, wcet_cycles, reason)
    """
    mnemonic = inst['mnemonic']
    operands = inst['clean_operands']

    # 1. PIO FIFO access (APB / AHB peripheral bus)
    if context.get('is_pio_read'):
        return (2, 2, "PIO RX FIFO read (APB/AHB bus)")
    if context.get('is_pio_write'):
        return (2, 2, "PIO TX FIFO write (APB/AHB bus)")

    # 2. Branch instructions
    if mnemonic == 'bl':
        return (2, 3, "Subroutine call (BL to via6522_read)")

    if mnemonic == 'bx' and 'lr' in operands:
        return (2, 3, "Subroutine return (BX LR)")

    if mnemonic.startswith('b') and not mnemonic.startswith('bic'):
        is_taken = context.get('is_taken', False)
        is_uncond = mnemonic in ['b', 'b.n', 'b.w']
        if is_uncond:
            return (2, 3, "Unconditional branch")
        elif is_taken:
            return (2, 3, "Conditional branch taken (pipeline refill)")
        else:
            return (1, 1, "Conditional branch not taken")

    if mnemonic == 'tbh':
        return (4, 5, "Table branch halfword (TBH read + pipeline refill)")
    if mnemonic == 'tbb':
        return (4, 5, "Table branch byte (TBB read + pipeline refill)")

    # 3. Memory Load / Store
    if mnemonic.startswith('ldr'):
        if '[pc' in operands:
            return (1, 2, "Literal load [pc-relative]")
        else:
            return (1, 2, "SRAM Load (hazard stall in WCET)")

    if mnemonic.startswith('str'):
        return (1, 2, "SRAM Store")

    # 4. Multiply
    if mnemonic in ['mul', 'mul.w', 'mla']:
        return (1, 1, "Single-cycle 32-bit Multiply")

    # 5. IT block
    if mnemonic.startswith('it'):
        return (1, 1, "IT block condition")

    # 6. Standard ALU / Logical / Shift
    return (1, 1, "ALU / Shift / Logic (Single-cycle)")

def analyze(target_file, clock_mhz=200, warn_cycles=50, deadline_cycles=70):
    objdump = find_objdump()
    if not objdump:
        raise RuntimeError("No suitable objdump found in PATH (tried arm-none-eabi-objdump, llvm-objdump, objdump)")

    disasm_lines = disassemble_functions(target_file, objdump)
    instructions, ordered_addrs = parse_disassembly(disasm_lines)

    # Find RX FIFO read in board
    start_addr = None
    for i, addr in enumerate(ordered_addrs):
        inst = instructions[addr]
        if inst['mnemonic'] in ['ldr', 'ldr.w'] and '#32' in inst['clean_operands']:
            for j in range(i + 1, min(i + 6, len(ordered_addrs))):
                next_inst = instructions[ordered_addrs[j]]
                if any(x in next_inst['clean_operands'] for x in ['#3840', '0xf00', '#2048', '0x800']):
                    start_addr = addr
                    break
        if start_addr is not None:
            break

    if start_addr is None:
        raise ValueError("Could not locate start point: mb_getaddr RX FIFO read (ldr [..., #32])")

    cycle_time_ns = 1000.0 / clock_mhz
    deadline_ns = deadline_cycles * cycle_time_ns
    warn_ns = warn_cycles * cycle_time_ns

    # Check whether board inlines via6522_read or calls it via BL
    idx = ordered_addrs.index(start_addr)
    common_path = []
    bl_inst = None
    reg_orb_target = None
    dispatch_table_addr = None
    is_tbb = False

    while idx < len(ordered_addrs):
        addr = ordered_addrs[idx]
        inst = instructions[addr]

        # Branch to valid IOSEL offset
        if inst['mnemonic'] in ['beq', 'beq.n', 'beq.w'] and idx > 0 and '#112' in instructions[ordered_addrs[idx - 1]]['clean_operands']:
            common_path.append(inst)
            target_str = inst['clean_operands'].split()[0]
            target_addr = int(target_str, 16)
            idx = ordered_addrs.index(target_addr)
            continue

        # Non-inlined branch to Read path (bne after cmp r2, #0)
        if inst['mnemonic'] in ['bne', 'bne.w', 'bne.n'] and idx > 0 and instructions[ordered_addrs[idx - 1]]['mnemonic'] == 'cmp' and 'r2' in instructions[ordered_addrs[idx - 1]]['clean_operands']:
            common_path.append(inst)
            target_str = inst['clean_operands'].split()[0]
            target_addr = int(target_str, 16)
            idx = ordered_addrs.index(target_addr)
            continue

        # Inlined branch to REG_ORB (bhi after cmp ..., #14)
        if inst['mnemonic'] in ['bhi', 'bhi.w', 'bhi.n'] and idx > 0 and '#14' in instructions[ordered_addrs[idx - 1]]['clean_operands']:
            reg_orb_target = int(inst['clean_operands'].split()[0], 16)
            next_inst = instructions[ordered_addrs[idx + 1]]
            dispatch_table_addr = next_inst['addr']
            is_tbb = (next_inst['mnemonic'] == 'tbb')
            common_path.append(inst)
            break

        # Function call to via6522_read
        if inst['mnemonic'] == 'bl' and '<via6522_read>' in inst['raw']:
            bl_inst = inst
            break

        common_path.append(inst)
        idx += 1

    results = []

    # Case A: Function call dispatch (10-17 version)
    if bl_inst is not None:
        post_idx = ordered_addrs.index(bl_inst['addr']) + 1
        post_path = []
        while post_idx < len(ordered_addrs):
            inst = instructions[ordered_addrs[post_idx]]
            post_path.append(inst)
            if inst['mnemonic'] in ['str', 'str.w'] and '#20' in inst['clean_operands']:
                break
            post_idx += 1

        via_addr = int(bl_inst['clean_operands'].split()[0], 16)
        via_idx = ordered_addrs.index(via_addr)
        via_common = []

        while via_idx < len(ordered_addrs):
            inst = instructions[ordered_addrs[via_idx]]
            via_common.append(inst)
            if inst['mnemonic'] in ['bhi', 'bhi.n', 'bhi.w']:
                reg_orb_target = int(inst['clean_operands'].split()[0], 16)
                tbl_inst = instructions[ordered_addrs[via_idx + 1]]
                dispatch_table_addr = tbl_inst['addr']
                is_tbb = (tbl_inst['mnemonic'] == 'tbb')
                break
            via_idx += 1

        targets = parse_tbb_table(instructions, ordered_addrs, dispatch_table_addr) if is_tbb else parse_tbh_table(instructions, ordered_addrs, dispatch_table_addr)
        targets[0] = reg_orb_target

        for reg in range(16):
            target = targets[reg]
            t_idx = ordered_addrs.index(target)
            reg_subpath = []
            while t_idx < len(ordered_addrs):
                inst = instructions[ordered_addrs[t_idx]]
                reg_subpath.append(inst)
                if inst['mnemonic'] == 'bx' and 'lr' in inst['clean_operands']:
                    break
                if inst['mnemonic'] in ['b', 'b.n', 'b.w']:
                    jump_addr = int(inst['clean_operands'].split()[0], 16)
                    t_idx = ordered_addrs.index(jump_addr)
                    continue
                t_idx += 1

            inst_seq = []
            total_nom = 0
            total_wcet = 0

            # 1. common_path
            for k, inst in enumerate(common_path):
                ctx = {}
                if k == 0:
                    ctx['is_pio_read'] = True
                elif inst['mnemonic'] in ['beq', 'beq.n', 'beq.w'] and '#112' in common_path[k - 1]['clean_operands']:
                    ctx['is_taken'] = True
                elif inst['mnemonic'] in ['bne', 'bne.n', 'bne.w']:
                    ctx['is_taken'] = True
                nom, wcet, reason = calc_inst_cycles(inst, ctx)
                total_nom += nom
                total_wcet += wcet
                inst_seq.append({'addr': inst['addr'], 'raw': inst['raw'], 'nom': nom, 'wcet': wcet, 'reason': reason})

            # 2. BL instruction
            nom, wcet, reason = calc_inst_cycles(bl_inst, {})
            total_nom += nom
            total_wcet += wcet
            inst_seq.append({'addr': bl_inst['addr'], 'raw': bl_inst['raw'], 'nom': nom, 'wcet': wcet, 'reason': reason})

            # 3. via_common
            for k, inst in enumerate(via_common):
                ctx = {}
                if inst['mnemonic'] in ['bhi', 'bhi.n', 'bhi.w']:
                    ctx['is_taken'] = (reg == 0)
                nom, wcet, reason = calc_inst_cycles(inst, ctx)
                total_nom += nom
                total_wcet += wcet
                inst_seq.append({'addr': inst['addr'], 'raw': inst['raw'], 'nom': nom, 'wcet': wcet, 'reason': reason})

            # 4. Table branch if reg != 0
            if reg != 0:
                tbl_inst = instructions[dispatch_table_addr]
                nom, wcet, reason = calc_inst_cycles(tbl_inst, {})
                total_nom += nom
                total_wcet += wcet
                inst_seq.append({'addr': tbl_inst['addr'], 'raw': tbl_inst['raw'], 'nom': nom, 'wcet': wcet, 'reason': reason})

            # 5. reg_subpath (inside via6522_read including bx lr)
            for k, inst in enumerate(reg_subpath):
                ctx = {}
                if inst['mnemonic'] in ['b', 'b.n', 'b.w']:
                    ctx['is_taken'] = True
                nom, wcet, reason = calc_inst_cycles(inst, ctx)
                total_nom += nom
                total_wcet += wcet
                inst_seq.append({'addr': inst['addr'], 'raw': inst['raw'], 'nom': nom, 'wcet': wcet, 'reason': reason})

            # 6. post_path (inside board to putdata)
            for k, inst in enumerate(post_path):
                ctx = {}
                if k == len(post_path) - 1:
                    ctx['is_pio_write'] = True
                nom, wcet, reason = calc_inst_cycles(inst, ctx)
                total_nom += nom
                total_wcet += wcet
                inst_seq.append({'addr': inst['addr'], 'raw': inst['raw'], 'nom': nom, 'wcet': wcet, 'reason': reason})

            total_inst = len(inst_seq)
            wcet_ns = total_wcet * cycle_time_ns
            nom_ns = total_nom * cycle_time_ns
            margin_ns = deadline_ns - wcet_ns
            margin_pct = (margin_ns / deadline_ns) * 100.0

            status = "PASS"
            if total_wcet > deadline_cycles:
                status = "FAIL"
            elif total_wcet > warn_cycles:
                status = "WARNING"

            results.append({
                'reg_idx': reg,
                'reg_name': REG_NAMES[reg],
                'target_addr': hex(target),
                'instruction_count': total_inst,
                'nominal_cycles': total_nom,
                'nominal_ns': nom_ns,
                'wcet_cycles': total_wcet,
                'wcet_ns': wcet_ns,
                'margin_cycles': deadline_cycles - total_wcet,
                'margin_ns': margin_ns,
                'margin_percent': margin_pct,
                'status': status,
                'instructions': inst_seq
            })

    # Case B: Inlined dispatch (10-26 version)
    else:
        targets = parse_tbh_table(instructions, ordered_addrs, dispatch_table_addr)
        targets[0] = reg_orb_target

        for reg in range(16):
            target = targets[reg]
            reg_path = trace_to_putdata(instructions, ordered_addrs, target)

            inst_seq = []
            total_nom = 0
            total_wcet = 0

            for i, inst in enumerate(common_path):
                ctx = {}
                if i == 0:
                    ctx['is_pio_read'] = True
                elif inst['mnemonic'] in ['beq', 'beq.n', 'beq.w'] and '#112' in common_path[i - 1]['clean_operands']:
                    ctx['is_taken'] = True
                elif inst['mnemonic'] in ['bhi', 'bhi.w', 'bhi.n']:
                    ctx['is_taken'] = (reg == 0)
                else:
                    ctx['is_taken'] = False

                nom, wcet, reason = calc_inst_cycles(inst, ctx)
                total_nom += nom
                total_wcet += wcet
                inst_seq.append({'addr': inst['addr'], 'raw': inst['raw'], 'nom': nom, 'wcet': wcet, 'reason': reason})

            if reg != 0:
                tbh_inst = instructions[dispatch_table_addr]
                nom, wcet, reason = calc_inst_cycles(tbh_inst, {})
                total_nom += nom
                total_wcet += wcet
                inst_seq.append({'addr': tbh_inst['addr'], 'raw': tbh_inst['raw'], 'nom': nom, 'wcet': wcet, 'reason': reason})

            for i, inst in enumerate(reg_path):
                ctx = {}
                if i == len(reg_path) - 1:
                    ctx['is_pio_write'] = True
                elif inst['mnemonic'] in ['b', 'b.n', 'b.w']:
                    ctx['is_taken'] = True

                nom, wcet, reason = calc_inst_cycles(inst, ctx)
                total_nom += nom
                total_wcet += wcet
                inst_seq.append({'addr': inst['addr'], 'raw': inst['raw'], 'nom': nom, 'wcet': wcet, 'reason': reason})

            total_inst = len(inst_seq)
            wcet_ns = total_wcet * cycle_time_ns
            nom_ns = total_nom * cycle_time_ns
            margin_ns = deadline_ns - wcet_ns
            margin_pct = (margin_ns / deadline_ns) * 100.0

            status = "PASS"
            if total_wcet > deadline_cycles:
                status = "FAIL"
            elif total_wcet > warn_cycles:
                status = "WARNING"

            results.append({
                'reg_idx': reg,
                'reg_name': REG_NAMES[reg],
                'target_addr': hex(target),
                'instruction_count': total_inst,
                'nominal_cycles': total_nom,
                'nominal_ns': nom_ns,
                'wcet_cycles': total_wcet,
                'wcet_ns': wcet_ns,
                'margin_cycles': deadline_cycles - total_wcet,
                'margin_ns': margin_ns,
                'margin_percent': margin_pct,
                'status': status,
                'instructions': inst_seq
            })

    longest = max(results, key=lambda x: x['wcet_cycles'])
    shortest = min(results, key=lambda x: x['wcet_cycles'])
    reg_t1cl = next(x for x in results if x['reg_idx'] == 4)
    reg_t1ch = next(x for x in results if x['reg_idx'] == 5)

    summary = {
        'target_file': target_file,
        'clock_mhz': clock_mhz,
        'deadline_cycles': deadline_cycles,
        'deadline_ns': deadline_ns,
        'warn_cycles': warn_cycles,
        'warn_ns': warn_ns,
        'longest_path': longest,
        'shortest_path': shortest,
        'reg_t1cl': reg_t1cl,
        'reg_t1ch': reg_t1ch,
        'all_registers': results
    }

    return summary

def print_text_report(summary, verbose=False):
    BLUE = '\033[0;34m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    RED = '\033[0;31m'
    BOLD = '\033[1m'
    NC = '\033[0m'

    print(f"{BOLD}======================================================================{NC}")
    print(f"{BOLD} a2picoMb Core 1 Bus Read Critical Path Cycle Analyzer                {NC}")
    print(f" Target Binary : {BOLD}{summary['target_file']}{NC}")
    print(f" Target MCU    : Cortex-M33 (RP2350 @ {summary['clock_mhz']} MHz, 1 cycle = {1000.0/summary['clock_mhz']:.1f} ns)")
    print(f" Deadline      : {summary['deadline_cycles']} cycles ({summary['deadline_ns']:.1f} ns)")
    print(f" Warning Gate  : {summary['warn_cycles']} cycles ({summary['warn_ns']:.1f} ns)")
    print(f"{BOLD}======================================================================{NC}")
    print(f"{'Register':18s} | {'Instrs':6s} | {'Nominal':10s} | {'WCET':11s} | {'Time (ns)':9s} | {'Margin':15s} | {'Status':7s}")
    print("-" * 88)

    has_fail = False
    has_warn = False

    for r in summary['all_registers']:
        status_color = GREEN
        if r['status'] == "FAIL":
            status_color = RED
            has_fail = True
        elif r['status'] == "WARNING":
            status_color = YELLOW
            has_warn = True

        margin_str = f"+{r['margin_cycles']:2d}c ({r['margin_percent']:4.1f}%)"
        print(f"{r['reg_name']:18s} | {r['instruction_count']:6d} | {r['nominal_cycles']:3d} cycles | {r['wcet_cycles']:3d} cycles | {r['wcet_ns']:7.1f} ns | {margin_str:15s} | {status_color}{r['status']}{NC}")

    print("-" * 88)
    longest = summary['longest_path']
    print(f"{BOLD}Critical Path (Worst-Case):{NC}")
    print(f"  Register        : {longest['reg_name']}")
    print(f"  Instructions    : {longest['instruction_count']} instructions")
    print(f"  Estimated Cycles: Nominal {longest['nominal_cycles']}c / WCET {longest['wcet_cycles']} cycles")
    print(f"  Response Latency: {longest['wcet_ns']:.1f} ns (@ {summary['clock_mhz']}MHz)")
    print(f"  Deadline Margin : {longest['margin_ns']:.1f} ns / {longest['margin_cycles']} cycles ({longest['margin_percent']:.1f}%)")
    print("")
    print(f"{BOLD}Key Registers Summary:{NC}")
    t1cl = summary['reg_t1cl']
    t1ch = summary['reg_t1ch']
    print(f"  REG_T1CL ($04)  : {t1cl['instruction_count']} instrs, {t1cl['wcet_cycles']}c ({t1cl['wcet_ns']:.1f} ns), Margin: +{t1cl['margin_cycles']}c ({t1cl['margin_percent']:.1f}%)")
    print(f"  REG_T1CH ($05)  : {t1ch['instruction_count']} instrs, {t1ch['wcet_cycles']}c ({t1ch['wcet_ns']:.1f} ns), Margin: +{t1ch['margin_cycles']}c ({t1ch['margin_percent']:.1f}%)")
    print(f"{BOLD}======================================================================{NC}")

    if verbose:
        print(f"\n{BOLD}[Detailed Instruction Trace for Longest Path: {longest['reg_name']}]{NC}")
        print(f"{'Address':10s} | {'Cyc(N/W)':8s} | {'Assembly Instruction':35s} | {'Cortex-M33 Pipeline Note'}")
        print("-" * 88)
        for inst in longest['instructions']:
            addr_str = f"0x{inst['addr']:x}"
            cyc_str = f"{inst['nom']}/{inst['wcet']}"
            parts = inst['raw'].split(None, 2)
            asm_part = parts[2] if len(parts) >= 3 else parts[1]
            print(f"{addr_str:10s} | {cyc_str:8s} | {asm_part:35s} | {inst['reason']}")
        print(f"{BOLD}======================================================================{NC}")

    if has_fail:
        print(f"{RED}{BOLD}🚨 GATE FAILURE: Read response latency exceeds deadline ({summary['deadline_cycles']} cycles / {summary['deadline_ns']:.1f} ns)!{NC}")
        return 2
    elif has_warn:
        print(f"{YELLOW}{BOLD}⚠ GATE WARNING: Read response latency exceeds warning threshold ({summary['warn_cycles']} cycles / {summary['warn_ns']:.1f} ns)!{NC}")
        return 0
    else:
        print(f"{GREEN}{BOLD}✅ ALL GATES PASSED: Core 1 Read Critical Path is well within safe margins.{NC}")
        return 0

def main():
    parser = argparse.ArgumentParser(description="Analyze Core 1 bus read critical path cycles for a2picoMb.")
    parser.add_argument("target", nargs="?", default=None, help="Target binary (board.c.o or .elf)")
    parser.add_argument("--clock-mhz", type=int, default=200, help="RP2350 CPU Clock MHz (default: 200)")
    parser.add_argument("--warn-cycles", type=int, default=50, help="Warning threshold in cycles (default: 50)")
    parser.add_argument("--deadline-cycles", type=int, default=70, help="Deadline budget in cycles (default: 70)")
    parser.add_argument("--verbose", "-v", action="store_true", help="Print detailed instruction-by-instruction trace")
    parser.add_argument("--json", action="store_true", help="Output results in JSON format")

    args = parser.parse_args()

    target = args.target
    if not target:
        root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        build_dir = os.path.join(root_dir, "build")
        if os.path.isdir(build_dir):
            elf_candidates = [os.path.join(build_dir, f) for f in os.listdir(build_dir) if f.endswith(".elf")]
            if elf_candidates:
                elf_candidates.sort(key=lambda x: os.path.getmtime(x), reverse=True)
                target = elf_candidates[0]
            else:
                for r, _, files in os.walk(build_dir):
                    for f in files:
                        if f in ["board.c.o", "board.c.obj"]:
                            target = os.path.join(r, f)
                            break
                    if target:
                        break

    if not target or not os.path.isfile(target):
        print(f"Error: Target binary not found: {target}", file=sys.stderr)
        print("Usage: inspect_critical_path.py [path/to/board.c.o or .elf]", file=sys.stderr)
        sys.exit(1)

    try:
        summary = analyze(target, clock_mhz=args.clock_mhz, warn_cycles=args.warn_cycles, deadline_cycles=args.deadline_cycles)
    except Exception as e:
        print(f"Error analyzing critical path: {e}", file=sys.stderr)
        sys.exit(1)

    if args.json:
        if not args.verbose:
            for r in summary['all_registers']:
                del r['instructions']
            del summary['longest_path']['instructions']
            del summary['shortest_path']['instructions']
            del summary['reg_t1cl']['instructions']
            del summary['reg_t1ch']['instructions']
        print(json.dumps(summary, indent=2))
        sys.exit(0)
    else:
        ret = print_text_report(summary, verbose=args.verbose)
        sys.exit(ret)

if __name__ == "__main__":
    main()
