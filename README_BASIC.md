# Minimal BASIC Interpreter

A lightweight BASIC interpreter designed for embedded systems with terminal I/O.

## Features

- **Minimal memory footprint**: ~21KB RAM
- **No dynamic allocation**: All static memory for embedded compatibility
- **Terminal-based I/O**: Works on any system with stdin/stdout
- **Traditional BASIC**: Line numbers, simple syntax

## Compilation

```bash
# Standard compilation
gcc -O2 -Wall -Wextra -std=c99 -o basic basic.c

# For embedded targets (example for ARM)
arm-none-eabi-gcc -Os -Wall -std=c99 -o basic.elf basic.c
```

## Supported Commands

### Immediate Commands (typed at prompt)
- `RUN` - Execute the program
- `LIST` - Display all program lines
- `NEW` - Clear the program and variables
- `QUIT` or `EXIT` - Exit the interpreter
- `HELP` - Display help information

### BASIC Statements

#### PRINT
Print text strings and numeric expressions.
```basic
10 PRINT "HELLO WORLD"
20 PRINT "A = ", A
30 PRINT "SUM = ", (A + B) * 2
```

#### LET
Assign values to variables (A-Z). The `LET` keyword is optional.
```basic
10 LET A = 5
20 B = 10
30 C = A + B
```

#### INPUT
Read user input into a variable.
```basic
10 INPUT A
20 INPUT "Enter your age: " B
```

#### GOTO
Jump to a line number.
```basic
10 PRINT "START"
20 GOTO 40
30 PRINT "SKIPPED"
40 PRINT "END"
```

#### END
Terminate program execution.
```basic
100 END
```

#### REM
Add comments to your program.
```basic
10 REM This is a comment
```

## Expression Syntax

- **Operators**: `+`, `-`, `*`, `/`
- **Parentheses**: `( )` for grouping
- **Variables**: Single letters A-Z
- **Numbers**: Integer constants
- **Precedence**: Standard (multiplication/division before addition/subtraction)

## Usage Examples

### Interactive Mode

```
$ ./basic
Minimal BASIC Interpreter
Type HELP for help, QUIT to exit
READY.
> 10 PRINT "HELLO WORLD"
> 20 END
> RUN
HELLO WORLD
READY.
> QUIT
Bye!
```

### Immediate Execution

```
> PRINT "IMMEDIATE MODE"
IMMEDIATE MODE
> LET X = 42
> PRINT "X = ", X
X =  42
```

### Loading Programs

You can pipe a program file into the interpreter:

```bash
cat demo.bas - << 'EOF' | ./basic
RUN
QUIT
EOF
```

## Example Programs

### Simple Calculator
```basic
10 REM Simple Calculator
20 PRINT "Enter first number:"
30 INPUT A
40 PRINT "Enter second number:"
50 INPUT B
60 PRINT "SUM = ", A + B
70 PRINT "PRODUCT = ", A * B
80 END
```

### Number Display
```basic
10 REM Display numbers using GOTO
20 A = 1
30 PRINT "Number: ", A
40 A = A + 1
50 GOTO 30
```
(Note: This creates an infinite loop - use Ctrl+C to stop)

## Limitations

- **Variables**: Only 26 variables (A-Z), integers only
- **Program size**: Maximum 256 lines
- **Line length**: Maximum 80 characters per line
- **No arrays**: Single variables only
- **No IF/THEN**: Use conditional GOTO workarounds (future enhancement)
- **No FOR/NEXT**: Use GOTO for loops (future enhancement)
- **No subroutines**: No GOSUB/RETURN (future enhancement)
- **No file I/O**: No SAVE/LOAD commands (future enhancement)

## Memory Usage

| Component | Size |
|-----------|------|
| Program storage | 20,480 bytes (256 lines × 80 chars) |
| Variables | 104 bytes (26 × 4 bytes) |
| Runtime state | ~400 bytes |
| **Total** | **~21 KB** |

## Technical Details

- **Architecture**: Single-pass interpreter
- **Storage**: Sorted array for program lines
- **Parser**: Recursive descent expression parser
- **Execution**: Line-by-line with GOTO for control flow
- **Language standard**: C99

## Future Enhancements

Possible additions (not in current version):
- `IF...THEN` conditional execution
- `FOR...NEXT` loops
- `GOSUB...RETURN` subroutines
- `DIM` for arrays
- String variables
- `SAVE`/`LOAD` program to file
- Relational operators in expressions
- `READ`/`DATA` statements

## Author

Created as a minimal BASIC interpreter for embedded systems with terminal I/O constraints.
