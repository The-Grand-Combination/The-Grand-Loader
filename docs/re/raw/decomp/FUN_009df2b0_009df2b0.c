
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_009df2b0(undefined4 param_1)

{
  LPMSG lpMsg;
  undefined4 *puVar1;
  undefined4 *puVar2;
  int iVar3;
  int *piVar4;
  char cVar5;
  int unaff_ESI;
  float10 fVar6;
  float fVar7;
  float fVar8;
  DWORD dwMilliseconds;
  
  cVar5 = '\x01';
  if (DAT_01258a74 != 0) {
    cVar5 = *(char *)(DAT_01258a74 + 0x6bca0);
  }
  if ((char)param_1 != '\0') {
    if (cVar5 == '\0') {
      dwMilliseconds = 100;
    }
    else {
      dwMilliseconds = 0;
    }
    Sleep(dwMilliseconds);
  }
  lpMsg = (LPMSG)(unaff_ESI + 0xf4);
  iVar3 = PeekMessageA(lpMsg,(HWND)0x0,0,0,1);
  while (iVar3 != 0) {
    TranslateMessage(lpMsg);
    DispatchMessageA(lpMsg);
    iVar3 = PeekMessageA(lpMsg,(HWND)0x0,0,0,1);
  }
  if (*(int *)(unaff_ESI + 0xf8) == 0x12) {
    *(undefined1 *)(unaff_ESI + 0xf1) = 1;
    return;
  }
  iVar3 = *(int *)(unaff_ESI + 0xec);
  fVar7 = (float)((double)*(float *)(iVar3 + 0x80) * DAT_00e455d8);
  fVar8 = (float)((double)*(float *)(iVar3 + 0x88) * DAT_00e455d8 * (double)fVar7);
  fVar7 = (float)((double)*(float *)(iVar3 + 0x84) * DAT_00e455d8 * (double)fVar7);
  if (DAT_00f1f698 != fVar7) {
    piVar4 = (int *)FUN_009c7290();
    (**(code **)(*piVar4 + 0x54))(fVar7);
  }
  if (DAT_00f1f694 != fVar8) {
    piVar4 = (int *)FUN_009e8090();
    (**(code **)(*piVar4 + 0x1c))(fVar8);
  }
  DAT_00f1f694 = fVar8;
  DAT_00f1f698 = fVar7;
  piVar4 = (int *)FUN_009c7290();
  if (DAT_00f20c18 == (undefined4 *)0x0) {
    DAT_00f20c18 = (undefined4 *)FUN_00aae9af(4);
    if (DAT_00f20c18 == (undefined4 *)0x0) {
      DAT_00f20c18 = (undefined4 *)0x0;
    }
    else {
      *DAT_00f20c18 = &PTR_FUN_00e38b28;
    }
  }
  fVar6 = (float10)(**(code **)(*DAT_01258a8c + 0x14))();
  (**(code **)(*piVar4 + 0x74))((float)fVar6);
  (**(code **)(**(int **)(unaff_ESI + 0xe4) + 0x24))
            ((int)((double)*(float *)(*(int *)(unaff_ESI + 0xec) + 0x9c) * DAT_00e455d8 *
                   _DAT_00e45618 + DAT_00e45580));
  if (*(int **)(unaff_ESI + 0x84) != (int *)0x0) {
    (**(code **)(**(int **)(unaff_ESI + 0x84) + 4))(param_1);
  }
  if (*(char *)(unaff_ESI + 0x88) != '\0') {
    if (*(int **)(unaff_ESI + 0x84) != (int *)0x0) {
      (**(code **)(**(int **)(unaff_ESI + 0x84) + 0x10))();
    }
    puVar1 = *(undefined4 **)(unaff_ESI + 0xe0);
    *(undefined4 *)(unaff_ESI + 0xe0) = 0;
    puVar2 = *(undefined4 **)(unaff_ESI + 0x84);
    if ((puVar1 != puVar2) && (puVar2 != (undefined4 *)0x0)) {
      (**(code **)*puVar2)(1);
    }
    *(undefined4 **)(unaff_ESI + 0x84) = puVar1;
    *(undefined1 *)(unaff_ESI + 0x88) = 0;
    (**(code **)(**(int **)(unaff_ESI + 0x84) + 8))
              (*(undefined4 *)(unaff_ESI + 0xe4),*(undefined4 *)(unaff_ESI + 0xe8));
    (**(code **)(**(int **)(unaff_ESI + 0x84) + 0xc))();
    (**(code **)(**(int **)(unaff_ESI + 0x84) + 0x14))();
  }
  return;
}

