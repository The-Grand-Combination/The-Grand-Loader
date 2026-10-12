
void __thiscall FUN_008b9360(int param_1,int param_2)

{
  undefined4 uVar1;
  
  if (param_2 == 0x405) {
    *(undefined1 *)(param_1 + 0x18) = 1;
    *(undefined4 *)(param_1 + 0x1c) = 0;
    return;
  }
  if (param_2 == 0x3de) {
    *(undefined1 *)(param_1 + 0x19) = 1;
    *(undefined4 *)(param_1 + 0x1c) = 0;
    return;
  }
  uVar1 = FUN_00aaf0c5(&stack0x00000008);
  *(undefined4 *)(param_1 + 0x1c) = uVar1;
  return;
}

