// SPDX-FileCopyrightText: 2017 rdci
//
// SPDX-License-Identifier: MIT

#pragma once

struct bucket{
	char * key;
	char * val;	
};

void ht_init(struct bucket * ht, int count);
int hash_add(char * key, char * val, int m, struct bucket ** hta);
int hash_rem(char * key, int m, struct bucket ** hta);
char * hash_getval(char * key, int m, struct bucket * ht);
