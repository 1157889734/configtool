#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fileconf.h"
#include "debug.h"


int main(int argc, char **argv)
{
	char acParseStr[8] = {0};
	int iRet  = 0;
	int iMode = 0;

    DebugInit(7788);
	InitConfigTool();
    
    while(1)
    {
        usleep(50000);
    }
    DebugUninit();
	return 0;
}

