#include <stdint.h>
#include <string.h>
#include <janet.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size){
	char *new_str = (char *)malloc(size+1);
	if (new_str == NULL){
		return 0;
	}
	memcpy(new_str, data, size);
	new_str[size] = '\0';

	/* janet logic */
	janet_init();
	JanetTable *env = janet_core_env(NULL);
	janet_dostring(env, new_str, "main", NULL);
	janet_deinit();

	free(new_str);
	return 0;
}

