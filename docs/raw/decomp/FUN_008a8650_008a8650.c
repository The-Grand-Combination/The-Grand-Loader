
undefined4 * __thiscall FUN_008a8650(undefined4 *param_1,undefined4 param_2)

{
  int iVar1;
  undefined4 *puVar2;
  void *local_10;
  undefined1 *puStack_c;
  undefined4 local_8;
  
  local_8 = 0xffffffff;
  puStack_c = &LAB_00b9f88b;
  local_10 = ExceptionList;
  ExceptionList = &local_10;
  param_1[3] = 0;
  puVar2 = DAT_01258a10;
  param_1[1] = 0x18d;
  *param_1 = CEventScope::vftable;
  param_1[2] = 0x2d2d2d;
  param_1[4] = 0;
  if (puVar2 == (undefined4 *)0x0) {
    puVar2 = (undefined4 *)FUN_00aae9af(0x278);
    local_8 = 0;
    if (puVar2 == (undefined4 *)0x0) {
      puVar2 = (undefined4 *)0x0;
      DAT_01258a10 = puVar2;
    }
    else {
      FUN_00954a40(puVar2);
      *puVar2 = CNullPop::vftable;
      DAT_01258a10 = puVar2;
    }
  }
  param_1[5] = puVar2;
  param_1[8] = param_2;
  param_1[0xc] = 0;
  param_1[6] = 0;
  *(undefined2 *)(param_1 + 7) = 1;
  param_1[9] = param_1;
  param_1[10] = 0;
  param_1[0xb] = 0x2d2d2d;
  param_1[0xd] = 0;
  param_1[0xe] = 0;
  param_1[0xf] = 0;
  param_1[0x10] = 1;
  iVar1 = param_1[8];
  if (iVar1 != 0) {
    param_1[0x10] = iVar1;
    param_1[0x11] = iVar1;
  }
  ExceptionList = local_10;
  return param_1;
}

