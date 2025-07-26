#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include "cJSON.h"
#define MODEL "gpt-4.1-nano"
#define MAX_PROMPT_LEN 4096
// add if debug to the debugging print statements
// write logging

struct memory {
	char *response;
	size_t size;
};

int get_auth(char *token) {
	//printf("Grabbing auth token...\n");
	char *home_path = getenv("HOME");
	char ext[] = "/.local/share/chat-cli/secrets.json";
	char token_path[128];
	snprintf(token_path, sizeof(token_path) / sizeof(char), "%s%s", home_path, ext);
	FILE *token_file = fopen(token_path, "r");

	char tokbuff[512];
	tokbuff[0] = '\0';
	char c;
	int idx = 0;
	while ((c = getc(token_file)) != EOF) {
		tokbuff[idx++] = (char)c;
	}

	tokbuff[idx] = '\0';
	fclose(token_file);

	cJSON *json = cJSON_Parse(tokbuff);

	if (json == NULL) {
		printf("Failed to parse json.\n");
		const char *err_ptr = cJSON_GetErrorPtr();
		if (err_ptr != NULL) {
			fprintf(stderr, "Error parsing json: %s\n", err_ptr);
		}
		return 1;
	} 

	cJSON *token_obj = cJSON_GetObjectItemCaseSensitive(json, "OPENAI_API_KEY");
	strncpy(token, token_obj->valuestring, strlen(token_obj->valuestring));

	cJSON_Delete(json);
	
	return 0;
}

struct curl_slist *build_headers() {
	//printf("Building headers...\n");

	struct curl_slist *headers = NULL;
	char auth_header[1024];
	auth_header[0] = '\0';

	// get auth token
	char token[512];
	if (get_auth(token)) {
		fprintf(stderr, "Failed to get auth token.\n");
		return NULL;
	};

	strcpy(auth_header, "Authorization: Bearer ");
	strncat(auth_header, token, strlen(token));

	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, auth_header);
	return headers;
}

char *get_prompt_header() {
	char hard_coded_header[] = "You are my in-console assistant. Respond only with text, never code snippets, images, or any formatting, only plain text. Respond with the tone of a servitor robot from Warhammer 40k.";
	char *ret_val = (char *)malloc(sizeof(char) * strlen(hard_coded_header));
	strncpy(ret_val, hard_coded_header, strlen(hard_coded_header));
	return ret_val;
}

char *build_request_json(const char *prompt) {
	//printf("Building request...\n");
	char *instructions = get_prompt_header();

	cJSON *json = cJSON_CreateObject();
	if (cJSON_AddStringToObject(json, "instructions", instructions) == NULL) {
		fprintf(stderr, "Failed to write \"model\" to cJSON object.\n");
		goto end;
	}

	if (cJSON_AddStringToObject(json, "model", MODEL) == NULL) {
		fprintf(stderr, "Failed to write \"model\" to cJSON object.\n");
		goto end;
	}

	if (cJSON_AddStringToObject(json, "input", prompt) == NULL) {
		fprintf(stderr, "Failed to write \"input\" to cJSON object.\n");
		goto end;
	}

	char *json_string = cJSON_Print(json);
	if (json_string == NULL) {
		fprintf(stderr, "Failed to print json request.\n");
		return NULL;
	}

	json_string[strlen(json_string)] = '\0';

	end:
		cJSON_Delete(json);
		return json_string;
}

// data: points to the delivered data
// size: always 1 (not sure why)
// nmemb: size of the data that 'data' points to.
// clientp: the pointer to the write field set by CURLOPT_WRITEDATA
static size_t cb(char *data, size_t size, size_t nmemb, void *clientp) {
	size_t realsize = size * nmemb;
	// cast the clientp to a memory struct.
	struct memory *mem = (struct memory *)clientp;
	// realloc mem's response value to fit the incoming chunk
	char *ptr = realloc(mem->response, mem->size + realsize + 1);
	if (!ptr) {
		fprintf(stderr, "Memory faliure creating response chunk\n");
		return 0;
	}
	// set mem's response pointer to the newly allocated memory
	mem->response = ptr;
	// copy realsize number of chars from incoming data to mem's response value,
	// offset by the current size of mem.
	memcpy(&(mem->response[mem->size]), data, realsize);
	// update the size
	mem->size += realsize;
	// terminate the string
	mem->response[mem->size] = 0;

	return realsize;
}

int post_prompt(const char *prompt) {
	//printf("Posting prompt...\n");

	struct curl_slist *headers = build_headers();
	if (headers == NULL) {
		fprintf(stderr, "Failed to get headers.");
	}

	// create a string and write the request json to it.
	char *request_json = build_request_json(prompt);
	if (request_json == NULL) {
		fprintf(stderr, "Failed to build request request.");
	}

	CURL *curl;
	CURLcode res_code;
	struct memory chunk = {0};

	// create a curl handle
	curl = curl_easy_init();

	if (curl == NULL) {
		printf("Failed to init curl instance.\n");
		fprintf(stderr, "Failed to init curl instance.\n");
	}

	if (curl) {
		// define curl options
		curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/responses");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

		//printf("Sending request...\n");
		res_code = curl_easy_perform(curl);

		//if (false) {
		if (res_code != CURLE_OK) {
			fprintf(stderr, "Http request failed: %s\n", curl_easy_strerror(res_code));
			return 1;
		}
		else if (chunk.response != NULL) {
	//		char mock_response[] = "{\"output\":[{\"content\":[{\"text\":\"Hello there. What do you want?\"}]}]}";
			cJSON *response = cJSON_Parse(chunk.response);
			if (response == NULL) {
				printf("Failed to parse response to json.\n");
				printf("DUMP: \n%s\n", chunk.response);
				const char *error_ptr = cJSON_GetErrorPtr();
				if (error_ptr != NULL) {
					fprintf(stderr, "Error: \n%s\n", error_ptr);
				}
				goto end;
			}

			cJSON *output = cJSON_GetObjectItemCaseSensitive(response, "output");
			if (output == NULL) {
				
			}
			cJSON *output_item = NULL;
			cJSON *content = NULL;
			cJSON *content_item = NULL;
			cJSON *text = NULL;

			cJSON_ArrayForEach(output_item, output) {
				content = cJSON_GetObjectItemCaseSensitive(output_item, "content");
					cJSON_ArrayForEach(content_item, content) {
						text = cJSON_GetObjectItemCaseSensitive(content_item, "text");
					}
			};

			putchar('\n');
			putchar('\n');
			int idx = 0;
			while (idx <= strlen(text->valuestring)) {
				printf("%c\0", text->valuestring[idx]);
				fflush(stdout);
				usleep(1000*10);
				idx++;
			}
			putchar('\n');
			putchar('\n');
		}
	}

end:
	// dont forget to clean everything up.
	curl_slist_free_all(headers);
	free(chunk.response);
	curl_easy_cleanup(curl);
	return 0;
}

int validate_prompt(char *prompt) {
	// check for one word prompt
	return 0;
}

int main (int argc, char *argv[]) {
	// validate args
	if (argc <= 1) {
		printf("Missing argument.\n");
		exit(0);
		// also ensure there aren't more args than expected.
		// this could mean accidental ommission of quotes.
	}

//	if (validate_prompt(argv[1])) {
//		printf("Invalid prompt. Only one word?");
//		exit(1);
//	}

	int input_len = strlen(argv[1]);

	if (input_len > MAX_PROMPT_LEN) {
		printf("Prompt too large. Brevity is a virtue.\n");
		exit(0);
	}

	const char *prompt = argv[1];
	
	if (post_prompt(prompt)) {
		printf("Failed posting prompt.\n");
	};

	// default, first arg is the message to send
	// check for one word messages. probably a mistake.
	//
	// parse response
	//
	// print to screen, one letter at a time but pretty fast.
	return 0;
}
