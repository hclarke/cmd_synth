#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#define MAX_TOKENS 2048
#define MAX_TOKEN_LEN 80
#define MAX_STACK 256

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
} component_type;

typedef struct component {
	component_type type;
	float value; //constants only
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

component* pop() {
	assert(stack_count > 0);
	stack_count -= 1;
	return stack[stack_count];
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

float* eval(component* comp, float sample_period, int sample_count);
float* alloc_buffer(int sample_count) {
	return (float*)malloc(sizeof(float) * sample_count);
}

float* eval_constant(component* comp, float sample_period, int sample_count) {
	float* b = comp->buffer = alloc_buffer(sample_count);
	float value = comp->value;
	for(int i = 0; i < sample_count; ++i) {
		b[i] = value;
	}
	return b;
}

float* eval_sin_wave(component* comp, float sample_period, int sample_count) {
	float* b = comp->buffer = alloc_buffer(sample_count);
	float* f = eval(comp->inputs[0], sample_period, sample_count);
	float t = 0;

	for(int i = 0; i < sample_count; ++i) {
		t += 2 * 3.14159 * f[i] * sample_period;
		b[i] = sin(t);
	}
	return b;
}

float* eval_mul(component* comp, float sample_period, int sample_count) {
	float* b = comp->buffer = alloc_buffer(sample_count);
	float* i0 = eval(comp->inputs[0], sample_period, sample_count);
	float* i1 = eval(comp->inputs[1], sample_period, sample_count);

	for(int i = 0; i < sample_count; ++i) {
		b[i] = i0[i] * i1[i];
	}
	return b;
}

float* eval_add(component* comp, float sample_period, int sample_count) {
	float* b = comp->buffer = alloc_buffer(sample_count);
	float* i0 = eval(comp->inputs[0], sample_period, sample_count);
	float* i1 = eval(comp->inputs[1], sample_period, sample_count);

	for(int i = 0; i < sample_count; ++i) {
		b[i] = i0[i] + i1[i];
	}
	return b;
}
float* eval_adsr(component* comp, float sample_period, int sample_count) {
	float* b = comp->buffer = alloc_buffer(sample_count);
	float* A = eval(comp->inputs[3], sample_period, sample_count);
	float* D = eval(comp->inputs[2], sample_period, sample_count);
	float* S = eval(comp->inputs[1], sample_period, sample_count);
	float* R = eval(comp->inputs[0], sample_period, sample_count);

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
float* eval(component* comp, float sample_period, int sample_count) {
	if(comp->buffer) {
		return comp->buffer;
	}
	switch(comp->type) {
		case constant:
			return eval_constant(comp, sample_period, sample_count);
		case sin_wave:
			return eval_sin_wave(comp, sample_period, sample_count);
		case mul:
			return eval_mul(comp, sample_period, sample_count);
		case add:
			return eval_add(comp, sample_period, sample_count);
		case adsr:
			return eval_adsr(comp, sample_period, sample_count);
		default:
			assert(0 && "unhandled type");
	}
}

float* execute(float sample_period, int sample_count) {
	for(int i = 0; i < token_count; ++i) {
		char* cmd = get_cmd(i);
		component* comp = (component*)malloc(sizeof(component));
		assert(comp);
		comp->buffer = NULL;
		comp->inputs = NULL;
		int arg_count = 0;
		if(1 == sscanf(cmd, "%f", &comp->value)) {
			comp->type = constant;
		}
		else if(0 == strcmp(cmd, "sin")) {
			comp->type = sin_wave;
			arg_count = 1; //frequency
		}
		else if(0 == strcmp(cmd, "mul")) {
			comp->type = mul;
			arg_count = 2; //frequency
		}
		else if(0 == strcmp(cmd, "add")) {
			comp->type = add;
			arg_count = 2; //frequency
		}
		else if(0 == strcmp(cmd, "adsr")) {
			comp->type = adsr;
			arg_count = 4; 
		}
		else {
			fprintf(stderr, "unknown command: [%s]\n", cmd);
			assert(0 && "unknown command");
		}

		//grab args
		comp->inputs = (component**)malloc(arg_count*sizeof(component*));
		for(int arg = 0; arg < arg_count; ++arg) {
			comp->inputs[arg] = pop();
		}

		push(comp);
	}

	return eval(pop(), sample_period, sample_count);
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
	uint32_t sample_count = (uint32_t)(sample_rate * duration);
	uint32_t data_size = sample_count *bytes_per_sample * channels;

	//run the program
	tokenize(in);
	float* fdata = execute(fsample_period, sample_count);
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