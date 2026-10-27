#include "common.h"
#include "task.h"

/* Empty function (RTS; NOP). Called from func_8C0985EC. 8C0986C4 stores it
 * as the default exec of a task, so it has the task-function signature
 * (sa1/engine.h: void func_8C098440(task *tp)). */
void func_8C098440(task *tp)
{
}
