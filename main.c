#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#define MAX_TOKENS 2048
#define MAX_TOKEN_LEN 80
#define MAX_STACK 256
#define MAX_VARS 2048
#define MAX_OPS 256

void putu32(FILE* f, uint32_t x) {
	for(int i = 0; i < 4; ++i) {
		putc(x&0xFF, f);
		x = x>>8;
	}
}

void putu16(FILE* f, uint16_t x) {
	for(int i = 0; i < 2; ++i) {
		putc(x&0xFF, f);
		x = x>>8;
	}
}

float sample_period;
int sample_count;
typedef struct component component;
typedef float* (*synth_op)(component*);
typedef enum {
	constant,
	operator,
	cmd_list,
} component_type;

struct component {
	component_type type;
	int opcode;
	float value; //constants only
	int start,count; //cmd_list only
	struct component** inputs;
	float* buffer;
};
float* eval(component* comp);

#define SYNTH_OP_NAME(name) name##_op_name
#define SYNTH_OP_DESC(name) name##_op_desc
#define SYNTH_OP_FUNC(name) name##_op_func
#define SYNTH_OP_ARGS(name) name##_op_args
#define SYNTH_OP_ITER(name) name##_op_iter
#define ARG(index) synth_op_args[index]
#define DEF_SYNTH_OP(name, desc, argc, state_type, state_init) \
char* SYNTH_OP_NAME(name) = #name;\
char* SYNTH_OP_DESC(name) = #desc;\
int   SYNTH_OP_ARGS(name) = argc;\
float SYNTH_OP_ITER(name)(float** synth_op_args,int i,state_type* state); \
float* SYNTH_OP_FUNC(name) (component* comp) {\
	float* b = comp->buffer = (float*)malloc(sizeof(float)*sample_count);\
	float* synth_op_args[argc]; \
	for(int i = 0; i < argc; ++i) { \
		synth_op_args[i] = eval(comp->inputs[i]);\
	}\
	state_type state = state_init;\
	for(int i = 0; i < sample_count; ++i) b[i] = SYNTH_OP_ITER(name)(synth_op_args,i,&state);\
	return b;\
}\
float SYNTH_OP_ITER(name)(float** synth_op_args,int i, state_type* state)

int op_count = 0;
char* op_names[MAX_OPS];
char* op_descs[MAX_OPS];
synth_op op_funcs[MAX_OPS];
int op_args[MAX_OPS];
#define REG_SYNTH_OP(name) do {\
	op_names[op_count] = SYNTH_OP_NAME(name);\
	op_descs[op_count] = SYNTH_OP_DESC(name);\
	op_funcs[op_count] = SYNTH_OP_FUNC(name);\
	op_args[op_count] = SYNTH_OP_ARGS(name);\
	op_count += 1;\
} while(0)

int token_count;
char program_data[MAX_TOKENS*MAX_TOKEN_LEN];

char* get_cmd(int i) {
	return program_data + i*MAX_TOKEN_LEN;
}

int stack_count = 0;
component* stack[MAX_STACK];

void execute(int start, int count);

component* pop(int execute_popped) {
	LOOP:
	assert(stack_count > 0);
	stack_count -= 1;

	component* top = stack[stack_count];
	if(execute_popped && cmd_list == top->type) {
		execute(top->start, top->count);
		goto LOOP;
	}
	return top;
}

component* peek() {
	assert(stack_count > 0);
	return stack[stack_count-1];
}

void push(component* comp) {
	assert(stack_count < MAX_STACK);
	stack[stack_count] = comp;
	stack_count += 1;
}

component* var_data[MAX_VARS];
char* var_names[MAX_VARS];
int var_count;

DEF_SYNTH_OP(sub, "subtracts two waveforms", 2, float, 0) {
	return ARG(0)[i] - ARG(1)[i];
}

DEF_SYNTH_OP(add, "add two waveforms", 2, float, 0) {
	return ARG(0)[i] + ARG(1)[i];
}
DEF_SYNTH_OP(mul, "multiply two waveforms componentwise", 2, float, 0) {
	return ARG(0)[i] * ARG(1)[i];
}
DEF_SYNTH_OP(abs, "add two waveforms", 1, float, 0) {
	return abs(ARG(0)[i]);
}

DEF_SYNTH_OP(sin, "generate a sine wave", 1, float, 0) {
	float x = sin(*state);
	*state += 2.0*3.14159*sample_period*ARG(0)[i];
	return x;
}
DEF_SYNTH_OP(log, "take the log of each sample. base 2^(1/12)", 1, float, 0) {
	return log2(ARG(0)[i])*12.0;
}

DEF_SYNTH_OP(exp, "take the exp of each sample. base 2^(1/12)", 1, float, 0) {
	return exp2(ARG(0)[i]/12.0);
}

DEF_SYNTH_OP(clip, "clamp each sample to the [-1,1] range", 1, float, 0) {
	return fmax(-1,fmin(1,ARG(0)[i]));
}


void register_ops() {
	REG_SYNTH_OP(sub);
	REG_SYNTH_OP(add);
	REG_SYNTH_OP(mul);
	REG_SYNTH_OP(abs);
	REG_SYNTH_OP(sin);
	REG_SYNTH_OP(log);
	REG_SYNTH_OP(exp);
	REG_SYNTH_OP(clip);
}

float* eval_constant(component* comp) {
	float* b = comp->buffer = (float*)malloc(sizeof(float)*sample_count);
	float value = comp->value;
	for(int i = 0; i < sample_count; ++i) {
		b[i] = value;
	}
	return b;
}

float* eval(component* comp) {
	if(comp->buffer) {
		return comp->buffer;
	}
	switch(comp->type) {
		case constant:
			return eval_constant(comp);
		case operator:
			return op_funcs[comp->opcode](comp);
		case cmd_list:
			assert(0);
		default:
			assert(0 && "unhandled type");
	}
}

int braces;
void execute(int start, int count) {
	for(int i = start; i < start+count; ++i) {
		char* cmd = get_cmd(i);
		
		if(braces > 0) {
			if(cmd[0] == ']') {
				braces -= 1;
			}
			else {
				peek()->count += 1;
			}
			continue;
		}
		component* comp = (component*)malloc(sizeof(component));
		assert(comp);
		comp->buffer = NULL;
		comp->inputs = NULL;
		int arg_count = 0;
		if(1 == sscanf(cmd, "%f", &comp->value)) {
			comp->type = constant;
		}
		else if(cmd[0] == '[') {
			comp->type = cmd_list;
			comp->start = i+1;
			comp->count = 0;
			braces += 1;
		}
		else if(cmd[0] == '@') {
			assert(var_count < MAX_VARS);
			var_names[var_count] = cmd+1;
			var_data[var_count] = pop(0);
			var_count += 1;
			free(comp);
			continue;
		}
		else if(cmd[0] == '%') { //pull nth to top of stack
			int index = atoi(cmd+1);
			for(int si = stack_count-index; si < stack_count; ++si) {
				component* temp = stack[si];
				stack[si] = stack[si-1];
				stack[si-1] = temp;
			}
			continue;
		}
		else {
			component* found = NULL;
			for(int v = 0; !found && v < var_count; ++v) {
				if(0 == strcmp(cmd, var_names[v])) {
					found = var_data[v];
				}
			}
			if(found) {
				push(found);
				free(comp);
				if(found->type == cmd_list) {
					push(pop(1)); //force cmd list to execute
				}
				continue;
			}

			int found_op = -1;
			for(int o = 0; o < op_count; ++o) {
				if(0 == strcmp(cmd, op_names[o])) {
					found_op = o;
					break;
				}
			}

			if(found_op >= 0) {
				comp->type = operator;
				comp->opcode = found_op;
				arg_count = op_args[found_op];
			}
			else {
				fprintf(stderr, "unknown command: [%s]\n", cmd);
				assert(0 && "unknown command");
			}
		}



		//grab args
		comp->inputs = (component**)malloc(arg_count*sizeof(component*));
		for(int arg = arg_count-1; arg >= 0; --arg) {
			comp->inputs[arg] = pop(1);
		}

		push(comp);
	}
}

void tokenize(FILE* in) {
	token_count = 0;
	char* cmd = program_data;
	while(token_count < MAX_TOKENS && 1 == fscanf(in, "%80s", cmd)) {
		token_count += 1;
		cmd += MAX_TOKEN_LEN;
	}
}

int main(int argc, char** argv) {
	int res;
	FILE* in = stdin;

	//get duration from arg
	assert(argc == 2);
	float duration;
	res=sscanf(argv[1], "%f", &duration);
	assert(res == 1);

	//set up sample rate and such
	static const uint16_t channels = 1;
	static const uint32_t bytes_per_sample = 2;
	static const uint32_t sample_rate = 44100;
	static const float fsample_rate = (float)sample_rate;
	static const float fsample_period = 1/fsample_rate;
	sample_count = (int)(sample_rate * duration);
	uint32_t data_size = sample_count *bytes_per_sample * channels;

	//run the program
	tokenize(in);
	var_count = 0;
	sample_period = fsample_period;
	register_ops();
	execute(0, token_count);
	float* fdata = eval(pop(1));
	assert(stack_count == 0 && "stack is not empty. this is probably wrong");

	//write WAV file
	FILE* f = stdout;
	assert(f);

	//RIFF chunk
	fprintf(f, "RIFF");
	putu32(f, data_size + 36);
	fprintf(f, "WAVE");

	//fmt chunk
	fprintf(f, "fmt ");
	putu32(f, 16); //size of this chunk
	putu16(f, 1); //1 = PCM
	putu16(f, channels);
	putu32(f, sample_rate);
	putu32(f, sample_rate * bytes_per_sample * channels);
	putu16(f, bytes_per_sample * channels);
	putu16(f, bytes_per_sample * 8);

	//data chunk
	fprintf(f, "data");
	putu32(f, data_size);

	for(int sample = 0; sample < sample_count; ++sample) {
		float x = fdata[sample];
		uint16_t pcm_sample = (int16_t)(x * 0x7FFF);
		putu16(f, pcm_sample);
	}
}