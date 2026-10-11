
undefined4 __thiscall FUN_008b9240(int param_1,undefined4 param_2)

{
  char cVar1;
  int iVar2;
  int iVar3;
  
  iVar2 = *(int *)(param_1 + 0xc) - *(int *)(param_1 + 8) >> 2;
  iVar3 = 0;
  if (0 < iVar2) {
    do {
      cVar1 = (**(code **)(**(int **)(*(int *)(param_1 + 8) + iVar3 * 4) + 0x18))(param_2);
      if (cVar1 == '\x01') {
        return 0;
      }
      iVar3 = iVar3 + 1;
    } while (iVar3 < iVar2);
  }
  return 1;
}

