# Second object file for multiple-obj test

.globl function2
function2:
    SUB32 R5, R6, R7
    AND32 R8, R9, R10
    .size function2, .-function2
