#ifndef _FILECONF_H_
#define _FILECONF_H_

#ifdef __cplusplus
extern "C"{
#endif /* End of #ifdef __cplusplus */

int ModifyParam(const char *pcFileName, const char *pcGroupKey, const char *pcKey, char *pcValue);
int ReadParam(const char *pcFileName, const char *pcGroupKey, const char *pcKey, char *pcValue);
int DeleteParam(const char *pcFileName, char *pcValue);

#ifdef __cplusplus
}
#endif


#endif
