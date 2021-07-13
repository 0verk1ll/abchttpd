// SPDX-FileCopyrightText: 2017 rdci
//
// SPDX-License-Identifier: MIT

#include "subr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void readcpy(char * dest, char * src, int cnt)
{
	while((*dest++=*src++) != '\0' && cnt-- != 0);	
	dest[cnt] = '\0';
}

int findelim(char delim, char * str){
	int i = 0;
	while(str[i] != delim && str[i] != '\0') ++i;
	return i;
}

int findws(char * str){
	int i = 0;
	while(str[i] != ' ' && str[i] != '\t' && str[i] != '\0') ++i;
	return i;
}
int intpow(int p, int x){
	if(x == 0) return 1;
	if(x == 1) return p;
	unsigned long r = p;
	while(x-- != 1)
		r *= p;
	return r;
}

int toint(char c){
	if(c >= 'A' && c <= 'F') return c-'7';
	if(c >= 'a' && c <= 'f') return c-'W';
	return c-'0';	
}
int hexval(char * buf)
{
	int sl = strlen(buf);
	if(sl == 0) return 0;
	long long z = 0, m = strlen(buf)-1;
	for(int i = 0; buf[i] != '\0'; i++, m--){
		z += toint(buf[i]) * intpow(16, m);
	}
	return z;
}
/* currently broken 
char * percdec(char * dest, char * str, int count)
{
        int di = 0;
	char wr = 0;
	char * nbuf = alloca(2*sizeof(char));
        for(int i = 0; str[i] != '\0' && di < count; i++){
                switch(str[i]){
                        case '+':
				wr = ' ';
				break;
                        case '%':
				wr = 'z';	
                                break;
                        default:
                                wr = str[i];
                                break;
                }
		dest[di++] = wr;
        }
	dest[di] = '\0';
	return dest;
}
*/

void die(char * msg){
	perror(msg);
	exit(-1);
}
/* split str delim
 * 
 * This function splits string str into tokens seperated by character delim.
 * It works by inserting null bytes into the string, storing the pointers of
 * the tokens as offsets of the string str. 
 * 
 */ 
char ** split(char * str, const char * delim, int count){
	if(count <= 0 || delim == NULL || str == NULL) return NULL;
	char ** tokens = calloc(count, sizeof(char *));
	char * last_token = NULL;
	int t = 0;
	if(!tokens) return NULL;
	last_token = str;
	for(int i = 0; str[i] != '\0'; ++i){
		for(size_t j = 0; j < strlen(delim); j++){
			if(str[i] == delim[j]){
				str[i] = '\0';
				if(strlen(last_token) > 0)
					tokens[t++] = last_token;
				last_token = str+i+1;
				if(t == count) goto ret;
				break;
			}
		}
	}
	tokens[t++] = last_token;
	if(t == 0){
		free(tokens);
		return NULL;
	}
ret:
	return tokens;
}

int countchar(char c, char * str)
{
	int z = 0;
	for(int i = 0; str[i] != '\0'; i++){
		if(str[i] == c) ++z;
	}
	return z;
}
