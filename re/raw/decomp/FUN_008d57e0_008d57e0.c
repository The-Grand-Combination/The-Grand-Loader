
uint __thiscall FUN_008d57e0(int param_1,int param_2)

{
  int iVar1;
  undefined4 local_c;
  uint local_8;
  
  if (*(char *)(param_1 + 0x18) == '\0') {
    if (*(char *)(param_1 + 0x19) == '\0') {
      local_c = *(undefined4 *)(param_1 + 0x1c);
      local_8 = *(uint *)(param_1 + 0x20);
    }
    else {
      local_c = *(undefined4 *)(*(int *)(param_2 + 0x24) + 0x2c);
      local_8 = *(uint *)(*(int *)(param_2 + 0x24) + 0x30);
    }
  }
  else {
    FUN_008a8dc0();
  }
  iVar1 = *(int *)(*(int *)(DAT_012587e4 + 4) + *(int *)(param_2 + 0xc) * 4);
  if ((local_8 != *(uint *)(iVar1 + 0x20)) &&
     (((local_8 = *(uint *)(*(int *)(iVar1 + 0xbe8) + local_8 * 4), *(int *)(local_8 + 0x34) != 0 ||
       (((local_8 = CONCAT31((int3)(local_8 >> 8),0x42), (char)local_c == 'R' &&
         ((char)((uint)local_c >> 8) == 'E')) && (local_c._2_1_ == 'B')))) ||
      (((*(char *)(iVar1 + 0x1c) == 'R' && (*(char *)(iVar1 + 0x1d) == 'E')) &&
       (*(char *)(iVar1 + 0x1e) == 'B')))))) {
    return CONCAT31((int3)(local_8 >> 8),1);
  }
  return local_8 & 0xffffff00;
}

