#include<stdio.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<unistd.h>
#include<string.h>
#include<signal.h>
#include<time.h>

#define MAX_TOKEN_SIZE 4197
#define MAX_NUM_TOKENS 4

char **tokenize(char *line)
{
    char **tokens = (char **)malloc(MAX_NUM_TOKENS * sizeof(char *));
    char *token = (char *)malloc(MAX_TOKEN_SIZE * sizeof(char));
    int i, tokenIndex = 0, tokenNo = 0, numPossibleTokens=MAX_NUM_TOKENS;
    int firstCmdFlag=0;

    //printf("%s ** %d\n", line, strlen(line));
    for(i =0; i <= strlen(line); i++){

        char readChar = line[i];
        if ((readChar == ' ' && tokenNo<numPossibleTokens) || readChar == '\0' || readChar == '\t' || readChar == '\n'){
            token[tokenIndex] = '\0';
            //printf("token = %s\n", token);
            if (tokenIndex != 0){
                tokens[tokenNo] = (char*)malloc(MAX_TOKEN_SIZE*sizeof(char));
                strcpy(tokens[tokenNo++], token);
                if(!firstCmdFlag)
                {
                    if((!strcmp(token, "create")) || (!strcmp(token, "update")))
                        numPossibleTokens = 3;
                    firstCmdFlag=1;
                }     
               //printf("**%s**\n", tokens[tokenNo-1]);
               tokenIndex = 0; 
            }
        } else {
            token[tokenIndex++] = readChar;
        }
    }
 
    free(token);
    tokens[tokenNo] = NULL ;
    return tokens;
}

                
