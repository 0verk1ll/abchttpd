// SPDX-FileCopyrightText: 2021 2017 rdci
//
// SPDX-License-Identifier: mit

#pragma once
/*
 * This file contains various subroutines.
 */

void readcpy(char * dest, char * src, int cnt);
int findws(char * str);
int findelim(char delim, char * str);
char ** split(char * str, const char * delim, int count);

int intpow(int p, int x);
int hexval(char * buf);
//char * percdec(char * dest, char * str, int count);
void die(char * msg);


int countchar(char c, char * str);
