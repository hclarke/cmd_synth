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

typedef enum {
	constant,
	sin_wave,
	mul,
	add,
	adsr,
	cmd_list,
	fexp,
	flog,
} component_type;

typedef struct component {
	component_type type;
	float value; //constants only
	int start,count; //cmd_list only
	struct component** inputs;
	float* buffer;
} component;

int token_count;
char program_data[MAX_TOKENS*MAX_TOKEN_LEN];

char* get_cmd(int i) {
	return program_data + i*MAX_TOKEN_LEN;
}

int stack_count = 0;
component* stack[MAX_STACK];
float sample_period;
int sample_count;

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

float* eval(component* comp);
float* alloc_buffer(int sample_count) {
	return (float*)malloc(sizeof(float) * sample_count);
}

float* eval_constant(component* comp) {
	float* b = comp->buffer = alloc_buffer(sample_count);
	float value = comp->value;
	for(int i = 0; i < sample_count; ++i) {
		b[i] = value;
	}
	return b;
}

float* eval_sin_wave(component* comp) {
	float* b = comp->buffer = alloc_buffer(sample_count);
	float* f = eval(comp->inputs[0]);
	float t = 0;

	for(int i = 0; i < sample_count; ++i) {
		t += 2 * 3.14159 * f[i] * sample_period;
		b[i] = sin(t);
	}
	return b;
}

float* eval_exp(component* comp) {
	float* b = comp->buffer = alloc_buffer(sample_count);
	float* f = eval(comp->inputs[0]);

	for(int i = 0; i < sample_count; ++i) {
		b[i] = exp2(f[i]/12.0);
	}
	return b;
}

float* eval_log(component* comp) {
	float* b = comp->buffer = alloc_buffer(sample_count);
	float* f = eval(comp->inputs[0]);

	for(int i = 0; i < sample_count; ++i) {
		b[i] = log2(f[i])*12.0;
	}
	return b;
}

float* eval_mul(component* comp) {
	float* b = comp->buffer = alloc_buffer(sample_count);
	float* i0 = eval(comp->inputs[0]);
	float* i1 = eval(comp->inputs[1]);

	for(int i = 0; i < sample_count; ++i) {
		b[i] = i0[i] * i1[i];
	}
	return b;
}

float* eval_add(component* comp) {
	float* b = comp->buffer = alloc_buffer(sample_count);
	float* i0 = eval(comp->inputs[0]);
	float* i1 = eval(comp->inputs[1]);

	for(int i = 0; i < sample_count; ++i) {
		b[i] = i0[i] + i1[i];
	}
	return b;
}

float* eval_adsr(component* comp) {
	float* b = comp->buffer = alloc_buffer(sample_count);
	float* A = eval(comp->inputs[3]);
	float* D = eval(comp->inputs[2]);
	float* S = eval(comp->inputs[1]);
	float* R = eval(comp->inputs[0]);

	float duration = sample_period * sample_count;

	for(int i = 0; i < sample_count; ++i) {
		float a = A[i];
		float d = D[i];
		float s = S[i];
		float r = R[i];

		float t = sample_period * i;

		if(t < a) {
			b[i] = t/a;
		}
		else if(t < a+d) {
			float u = (t-a)/d;

			b[i] = s*u + 1 - u;
		}
		else if(t > duration - r) {
			float u = (duration-t)/r;

			b[i] = s * u;
		}
		else {
			b[i] = s;
		}
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
		case sin_wave:
			return eval_sin_wave(comp);
		case mul:
			return eval_mul(comp);
		case fexp:
			return eval_exp(comp);
		case flog:
			return eval_log(comp);
		case add:
			return eval_add(comp);
		case adsr:
			return eval_adsr(comp);
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
		else if(0 == strcmp(cmd, "sin")) {
			comp->type = sin_wave;
			arg_count = 1;
		}
		else if(0 == strcmp(cmd, "mul")) {
			comp->type = mul;
			arg_count = 2;
		}
		else if(0 == strcmp(cmd, "exp")) {
			comp->type = fexp;
			arg_count = 1; 
		}
		else if(0 == strcmp(cmd, "log")) {
			comp->type = flog;
			arg_count = 1; 
		}
		else if(0 == strcmp(cmd, "add")) {
			comp->type = add;
			arg_count = 2;
		}
		else if(0 == strcmp(cmd, "adsr")) {
			comp->type = adsr;
			arg_count = 4; 
		}
		else {
			component* found = NULL;
			for(int v = 0; v < var_count; ++v) {
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
			fprintf(stderr, "unknown command: [%s]\n", cmd);
			assert(0 && "unknown command");
		}

		//grab args
		comp->inputs = (component**)malloc(arg_count*sizeof(component*));
		for(int arg = 0; arg < arg_count; ++arg) {
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