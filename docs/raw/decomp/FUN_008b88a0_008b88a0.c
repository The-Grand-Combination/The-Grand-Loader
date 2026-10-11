
undefined4 __thiscall FUN_008b88a0(int param_1,undefined4 param_2)

{
  char cVar1;
  int iVar2;
  int iVar3;
  
  iVar2 = 0;
  iVar3 = *(int *)(param_1 + 0xc) - *(int *)(param_1 + 8) >> 2;
  if (0 < iVar3) {
    do {
      cVar1 = (**(code **)(**(int **)(*(int *)(param_1 + 8) + iVar2 * 4) + 0x18))(param_2);
      if (cVar1 == '\x01') {
        return 1;
      }
      iVar2 = iVar2 + 1;
    } while (iVar2 < iVar3);
  }
  return 0;
}

