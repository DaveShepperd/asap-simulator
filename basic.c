/* MIT license. See LICENSE.md for details */

/*
 * Minimal BASIC Interpreter for Embedded Systems
 * Features: PRINT, LET, INPUT, GOTO, END, REM
 * Target: Terminal I/O only, minimal memory footprint (~21KB)
 */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"
#include "stdbool.h"
#include "stdint.h"

/* Configuration */
#define MAX_LINES 256        /* Maximum program lines */
#define MAX_LINE_LEN 80      /* Maximum characters per line */
#define MAX_VARS 26          /* Variables A-Z only */

/* Line storage structure */
typedef struct {
    uint16_t lineNum;        /* Line number (0 = empty slot) */
    char text[MAX_LINE_LEN]; /* Actual BASIC statement */
} Line_t;

/* Runtime state */
typedef struct {
    Line_t program[MAX_LINES];  /* Program storage */
    int32_t vars[MAX_VARS];     /* Variable storage A-Z */
    int programSize;            /* Number of lines stored */
    int currentLine;            /* Current line index during execution */
    bool running;               /* Execution state flag */
    char inputBuf[MAX_LINE_LEN]; /* Input buffer */
    char errorMsg[80];          /* Last error message */
} Basic_t;

/* Forward declarations */
static void initBasic(Basic_t *b);
static const char* skipWhitespace(const char *s);
static int32_t parseExpression(Basic_t *b, const char **input);
static int32_t parseTerm(Basic_t *b, const char **input);
static int32_t parseFactor(Basic_t *b, const char **input);
static void storeLine(Basic_t *b, int lineNum, const char *text);
static void deleteLine(Basic_t *b, int lineNum);
static int findLine(Basic_t *b, int lineNum);
static void listProgram(Basic_t *b);
static void newProgram(Basic_t *b);
static int executeStatement(Basic_t *b, const char *stmt);
static int execPrint(Basic_t *b, const char *args);
static int execLet(Basic_t *b, const char *args);
static int execInput(Basic_t *b, const char *args);
static int execGoto(Basic_t *b, const char *args);
static void processCommand(Basic_t *b, const char *line);
static void executeNext(Basic_t *b);

/* Initialize interpreter state */
static void initBasic(Basic_t *b) {
    memset(b, 0, sizeof(Basic_t));
    b->programSize = 0;
    b->currentLine = 0;
    b->running = false;
}

/* Skip whitespace in string */
static const char* skipWhitespace(const char *s) {
    while (*s && isspace(*s)) s++;
    return s;
}

/* Parse a factor: NUMBER | VARIABLE | '(' expression ')' */
static int32_t parseFactor(Basic_t *b, const char **input) {
    const char *s = skipWhitespace(*input);

    /* Negative number */
    if (*s == '-') {
        s++;
        *input = s;
        return -parseFactor(b, input);
    }

    /* Parenthesized expression */
    if (*s == '(') {
	int32_t val;
        s++;
        *input = s;
        val = parseExpression(b, input);
        s = skipWhitespace(*input);
        if (*s == ')') s++;
        *input = s;
        return val;
    }

    /* Variable A-Z */
    if (isalpha(*s)) {
        char varName = toupper(*s);
        s++;
        *input = s;
        return b->vars[varName - 'A'];
    }

    /* Number */
    if (isdigit(*s)) {
        int32_t val = 0;
        while (isdigit(*s)) {
            val = val * 10 + (*s - '0');
            s++;
        }
        *input = s;
        return val;
    }

    return 0;
}

/* Parse a term: factor (('*' | '/') factor)* */
static int32_t parseTerm(Basic_t *b, const char **input) {
    int32_t val = parseFactor(b, input);
    const char *s = skipWhitespace(*input);

    while (*s == '*' || *s == '/') {
        char op = *s;
	int32_t right;
        s++;
        *input = s;
        right = parseFactor(b, input);

        if (op == '*') {
            val = val * right;
        } else {
            if (right == 0) {
                snprintf(b->errorMsg, sizeof(b->errorMsg), "Division by zero");
                return 0;
            }
            val = val / right;
        }

        s = skipWhitespace(*input);
    }

    *input = s;
    return val;
}

/* Parse an expression: term (('+' | '-') term)* */
static int32_t parseExpression(Basic_t *b, const char **input) {
    int32_t val = parseTerm(b, input);
    const char *s = skipWhitespace(*input);

    while (*s == '+' || *s == '-') {
        char op = *s;
	int32_t right;
        s++;
        *input = s;
        right = parseTerm(b, input);

        if (op == '+') {
            val = val + right;
        } else {
            val = val - right;
        }

        s = skipWhitespace(*input);
    }

    *input = s;
    return val;
}

/* Store or replace a line in the program */
static void storeLine(Basic_t *b, int lineNum, const char *text) {
    int i;

    /* Skip leading whitespace in text */
    text = skipWhitespace(text);

    /* Find insertion point (keep sorted by line number) */
    for (i = 0; i < b->programSize; i++) {
        if (b->program[i].lineNum == lineNum) {
            /* Replace existing line */
            strncpy(b->program[i].text, text, MAX_LINE_LEN - 1);
            b->program[i].text[MAX_LINE_LEN - 1] = '\0';
            return;
        } else if (b->program[i].lineNum > lineNum) {
            break;
        }
    }

    /* Insert new line */
    if (b->programSize >= MAX_LINES) {
        printf("Program full\n");
        return;
    }

    /* Shift lines down to make room */
    if (i < b->programSize) {
        memmove(&b->program[i + 1], &b->program[i],
                (b->programSize - i) * sizeof(Line_t));
    }

    /* Insert new line */
    b->program[i].lineNum = lineNum;
    strncpy(b->program[i].text, text, MAX_LINE_LEN - 1);
    b->program[i].text[MAX_LINE_LEN - 1] = '\0';
    b->programSize++;
}

/* Delete a line from the program */
static void deleteLine(Basic_t *b, int lineNum) {
    int i;

    for (i = 0; i < b->programSize; i++) {
        if (b->program[i].lineNum == lineNum) {
            /* Shift remaining lines up */
            if (i < b->programSize - 1) {
                memmove(&b->program[i], &b->program[i + 1],
                        (b->programSize - i - 1) * sizeof(Line_t));
            }
            b->programSize--;
            return;
        }
    }
}

/* Find line index by line number, return -1 if not found */
static int findLine(Basic_t *b, int lineNum) {
    int i;
    for (i = 0; i < b->programSize; i++) {
        if (b->program[i].lineNum == lineNum) {
            return i;
        }
    }
    return -1;
}

/* List all program lines */
static void listProgram(Basic_t *b) {
    int i;
    if (b->programSize == 0) {
        printf("No program loaded\n");
        return;
    }

    for (i = 0; i < b->programSize; i++) {
        printf("%d %s\n", b->program[i].lineNum, b->program[i].text);
    }
}

/* Clear the program */
static void newProgram(Basic_t *b) {
    b->programSize = 0;
    b->currentLine = 0;
    b->running = false;
    memset(b->vars, 0, sizeof(b->vars));
    memset(b->program, 0, sizeof(b->program));
}

/* Execute PRINT statement */
static int execPrint(Basic_t *b, const char *args) {
    args = skipWhitespace(args);

    while (*args && *args != '\n' && *args != '\0') {
        args = skipWhitespace(args);

        if (*args == '"') {
            /* String literal */
            args++;
            while (*args && *args != '"') {
                putchar(*args++);
            }
            if (*args == '"') args++;
        } else if (*args == ',') {
            /* Comma separator - add space */
            putchar(' ');
            args++;
        } else if (*args == ';') {
            /* Semicolon separator - no space */
            args++;
        } else if (isalpha(*args) || isdigit(*args) || *args == '(' || *args == '-') {
            /* Expression */
            const char *start = args;
            int32_t val = parseExpression(b, &args);
            if (args == start) break; /* No progress */
            printf("%d", val);
        } else {
            break;
        }
    }

    putchar('\n');
    return 0;
}

/* Execute LET statement (or implicit assignment) */
static int execLet(Basic_t *b, const char *args) {
    int32_t value;
    char varName;
    args = skipWhitespace(args);

    /* Skip optional LET keyword */
    if (strncmp(args, "LET", 3) == 0 && isspace(args[3])) {
        args += 3;
        args = skipWhitespace(args);
    }

    /* Get variable name */
    if (!isalpha(*args)) {
        snprintf(b->errorMsg, sizeof(b->errorMsg), "Expected variable name");
        return -1;
    }
    varName = toupper(*args);
    args++;

    /* Expect '=' */
    args = skipWhitespace(args);
    if (*args != '=') {
        snprintf(b->errorMsg, sizeof(b->errorMsg), "Expected '='");
        return -1;
    }
    args++;

    /* Evaluate expression */
    value = parseExpression(b, &args);
    b->vars[varName - 'A'] = value;

    return 0;
}

/* Execute INPUT statement */
static int execInput(Basic_t *b, const char *args) {
    char varName;
    int32_t value;
    args = skipWhitespace(args);

    /* Optional prompt string */
    if (*args == '"') {
        args++;
        while (*args && *args != '"') {
            putchar(*args++);
        }
        if (*args == '"') args++;

        /* Skip optional semicolon or comma after prompt */
        args = skipWhitespace(args);
        if (*args == ';' || *args == ',') args++;
    }

    args = skipWhitespace(args);

    /* Get variable name */
    if (!isalpha(*args)) {
        snprintf(b->errorMsg, sizeof(b->errorMsg), "Expected variable name");
        return -1;
    }
    varName = toupper(*args);

    /* Prompt and read */
    printf("? ");
    fflush(stdout);

    if (fgets(b->inputBuf, MAX_LINE_LEN, stdin) == NULL) {
        return -1;
    }

    /* Parse number */
    value = atoi(b->inputBuf);
    b->vars[varName - 'A'] = value;

    return 0;
}

/* Execute GOTO statement */
static int execGoto(Basic_t *b, const char *args) {
    int targetLine, idx;
    args = skipWhitespace(args);

    if (!isdigit(*args)) {
        snprintf(b->errorMsg, sizeof(b->errorMsg), "Expected line number");
        return -1;
    }

    targetLine = atoi(args);
    idx = findLine(b, targetLine);

    if (idx < 0) {
        snprintf(b->errorMsg, sizeof(b->errorMsg), "Line %d not found", targetLine);
        return -1;
    }

    /* Set to idx - 1 because executeNext() will increment it */
    b->currentLine = idx - 1;
    return 0;
}

/* Execute a single statement */
static int executeStatement(Basic_t *b, const char *stmt) {
    stmt = skipWhitespace(stmt);

    /* Empty line or comment */
    if (*stmt == '\0' || *stmt == '\n') {
        return 0;
    }

    /* REM - comment */
    if (strncmp(stmt, "REM", 3) == 0) {
        return 0;
    }

    /* PRINT */
    if (strncmp(stmt, "PRINT", 5) == 0) {
        return execPrint(b, stmt + 5);
    }

    /* LET */
    if (strncmp(stmt, "LET", 3) == 0) {
        return execLet(b, stmt + 3);
    }

    /* INPUT */
    if (strncmp(stmt, "INPUT", 5) == 0) {
        return execInput(b, stmt + 5);
    }

    /* GOTO */
    if (strncmp(stmt, "GOTO", 4) == 0) {
        return execGoto(b, stmt + 4);
    }

    /* END */
    if (strncmp(stmt, "END", 3) == 0) {
        b->running = false;
        return 0;
    }

    /* Implicit LET (variable assignment without LET keyword) */
    if (isalpha(*stmt)) {
        const char *p = stmt + 1;
        p = skipWhitespace(p);
        if (*p == '=') {
            return execLet(b, stmt);
        }
    }

    snprintf(b->errorMsg, sizeof(b->errorMsg), "Unknown command");
    return -1;
}

/* Execute next line in program */
static void executeNext(Basic_t *b) {
    const char *stmt;
    Line_t *line;

    if (b->currentLine >= b->programSize) {
        b->running = false;
        return;
    }

    line = &b->program[b->currentLine];
    stmt = line->text;

    /* Execute statement */
    if (executeStatement(b, stmt) < 0) {
        printf("Error at line %d: %s\n", line->lineNum, b->errorMsg);
        b->running = false;
        return;
    }

    /* Advance to next line (unless GOTO changed it) */
    if (b->running) {
        b->currentLine++;
    }
}

/* Process command from user input */
static void processCommand(Basic_t *b, const char *line) {
    int lineNum;

    line = skipWhitespace(line);

    /* Empty line */
    if (*line == '\0' || *line == '\n') {
        return;
    }

    /* Check if line starts with number */
    if (isdigit(*line)) {
        lineNum = atoi(line);

        /* Skip line number */
        while (isdigit(*line)) line++;
        line = skipWhitespace(line);

        if (*line == '\0' || *line == '\n') {
            /* Delete line */
            deleteLine(b, lineNum);
        } else {
            /* Store line */
            storeLine(b, lineNum, line);
        }
    } else {
        /* Immediate command */
        if (strncmp(line, "RUN", 3) == 0) {
            b->currentLine = 0;
            b->running = true;
            memset(b->vars, 0, sizeof(b->vars)); /* Clear variables */
        } else if (strncmp(line, "LIST", 4) == 0) {
            listProgram(b);
        } else if (strncmp(line, "NEW", 3) == 0) {
            newProgram(b);
            printf("New program\n");
        } else if (strncmp(line, "QUIT", 4) == 0 || strncmp(line, "EXIT", 4) == 0) {
            printf("Bye!\n");
            exit(0);
        } else {
            /* Try to execute as immediate statement */
            if (executeStatement(b, line) < 0) {
                printf("Error: %s\n", b->errorMsg);
            }
        }
    }
}

/* Main program */
int main(void) {
    Basic_t basic;
    initBasic(&basic);

    printf("Minimal BASIC Interpreter\n");
    printf("Type HELP for help, QUIT to exit\n");
    printf("READY.\n");

    /* REPL loop */
    while (1) {
        if (basic.running) {
            /* Run mode - execute program */
            executeNext(&basic);
            if (!basic.running) {
                printf("READY.\n");
            }
        } else {
            /* Command mode - accept input */
            printf("> ");
            fflush(stdout);

            if (fgets(basic.inputBuf, MAX_LINE_LEN, stdin) == NULL) {
                break;
            }

            /* Handle HELP command */
            if (strncmp(basic.inputBuf, "HELP", 4) == 0) {
                printf("Commands:\n");
                printf("  RUN      - Run the program\n");
                printf("  LIST     - List the program\n");
                printf("  NEW      - Clear the program\n");
                printf("  QUIT     - Exit interpreter\n");
                printf("\nStatements:\n");
                printf("  PRINT    - Print text or expressions\n");
                printf("  LET      - Assign value to variable (A-Z)\n");
                printf("  INPUT    - Read user input\n");
                printf("  GOTO     - Jump to line number\n");
                printf("  END      - End program\n");
                printf("  REM      - Comment\n");
                printf("\nExample:\n");
                printf("  10 LET A = 5\n");
                printf("  20 PRINT \"A = \", A\n");
                printf("  30 END\n");
                printf("  RUN\n");
                continue;
            }

            processCommand(&basic, basic.inputBuf);
        }
    }

    return 0;
}
