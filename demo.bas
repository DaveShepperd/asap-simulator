10 REM Demonstration of Minimal BASIC
20 PRINT "==================================="
30 PRINT "  MINIMAL BASIC DEMONSTRATION"
40 PRINT "==================================="
50 PRINT ""
60 REM Test variables and expressions
70 PRINT "Testing variables and math..."
80 LET A = 10
90 LET B = 20
100 PRINT "A = ", A
110 PRINT "B = ", B
120 PRINT "A + B = ", A + B
130 PRINT "A * B = ", A * B
140 PRINT "(A + B) * 2 = ", (A + B) * 2
150 PRINT ""
160 REM Test GOTO
170 PRINT "Testing GOTO..."
180 PRINT "Jumping to line 220"
190 GOTO 220
200 PRINT "This should be skipped"
210 PRINT "This too"
220 PRINT "Landed at line 220!"
230 PRINT ""
240 PRINT "Program complete!"
250 END
