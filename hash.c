#include "hash.h"
#include "subr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <alloca.h>

#define HASH_FUNC djb2

/* www.cse.yorku.ca/~oz/hash.html */
unsigned long djb2(unsigned char * str)
{
        unsigned long hash = 5381;
        int c;

        while((c = *str++))
                hash = ((hash << 5) + hash) + c;        /* hash * 33 + c */

        return hash;
}

unsigned char * unsstr(char * str)
{	
	assert(str != NULL);
	unsigned char * s, * r;
	s = r = (unsigned char *)str;
	while((*s++ = (unsigned char)*str++));
	return r;
}
long hcompute(char * str, int htsize)
{
	unsigned char * us = NULL;
	char * strc = alloca(128);
	readcpy(strc, str, 128);
	
	us = unsstr(strc);
	
	unsigned long h = HASH_FUNC(us);
	return h % htsize;		
}

void ht_init(struct bucket * ht, int count)
{
	assert(ht != NULL && count > 0);
	for(int i = 0; i < count; i++){
		ht[i].key = NULL;
		ht[i].val = NULL;	
	}
}

int hash_add(char * key, char * val, int m, struct bucket ** hta)
{
	struct bucket * ht = *hta;

	assert(key != NULL);
	assert(hta != NULL && ht != NULL);
	
	int l = hcompute(key, m);
	int ls = l;
	
	if(ht[l].key == NULL){
		ht[l].key = key;
		ht[l].val = val;
	}else{
		while(&ht[l] != NULL && ht[l++].key != NULL){
			if(l == m) l = 0;	/* wrap around */
			if(l == ls) return -1;	/* no space */	
		}
		ht[l].key = key;
		ht[l].val = val;
	}
	return 0;
}

struct bucket * hash_ifind(char * key, int l, int m, struct bucket *hta){
	assert(key != NULL && m > 0 && l >= 0 && l <= m && hta != NULL);
	
	struct bucket * ht = hta;
	int ls = l;
	while(l != m){
		if(&ht[l] != NULL && ht[l].key != NULL &&
			strcmp(ht[l].key, key) == 0){
			return &ht[l];
		}
		l++;
		if(l == m) l = 0;
		if(l == ls) break;
	}
	return NULL;
}

int hash_rem(char * key, int m, struct bucket ** hta)
{
	assert(key != NULL);
	assert(hta != NULL);
	
	int l = hcompute(key, m);

	struct bucket * ht = hash_ifind(key, l, m, *hta);
	
	ht->key = NULL;
	ht->val = NULL;
	return 0;
}
char * hash_getval(char * key, int m, struct bucket * ht)
{
	assert(key != NULL && ht != NULL);
	assert(m > 0);

	int l = hcompute(key, m);
	
	struct bucket * r = hash_ifind(key, l, m, ht);
	if(r == NULL)
		return NULL;
	return r->val;
}
