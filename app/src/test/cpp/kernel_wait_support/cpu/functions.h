#pragma once
// CPU register access/stopping are external boundaries. Callback context preparation,
// restoration and freeze gates remain production code.
#include <cpu/common.h>
struct CPUState;
void stop(CPUState &cpu);
uint32_t read_sp(CPUState &cpu);
uint32_t read_tpidruro(CPUState &cpu);
void write_reg(CPUState &cpu, size_t index, uint32_t value);
void write_sp(CPUState &cpu, uint32_t value);
void write_pc(CPUState &cpu, uint32_t value);
void write_lr(CPUState &cpu, uint32_t value);
void write_tpidruro(CPUState &cpu, uint32_t value);
CPUContext save_context(CPUState &cpu);
void load_context(CPUState &cpu, const CPUContext &context);
