
void __thiscall FUN_008d3f50(int param_1,int param_2)

{
  undefined4 *puVar1;
  undefined1 local_c [8];
  
  if (param_2 == 0x405) {
    *(undefined1 *)(param_1 + 0x18) = 1;
    return;
  }
  if (param_2 == 0x3de) {
    *(undefined1 *)(param_1 + 0x19) = 1;
    return;
  }
  puVar1 = (undefined4 *)FUN_0055c6c0(DAT_012587e4,local_c,&stack0x00000008);
  *(undefined4 *)(param_1 + 0x1c) = *puVar1;
  *(undefined4 *)(param_1 + 0x20) = puVar1[1];
  return;
}

