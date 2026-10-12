
void __thiscall FUN_0090a1e0(int param_1,int param_2,int param_3)

{
  int iVar1;
  int iVar2;
  int *piVar3;
  
  if (param_3 < 0x89a) {
    if (param_3 == 0x899) {
      FUN_009a1bb0(param_1 + 0x131);
      return;
    }
    if (param_3 < 0x3c0) {
      if (param_3 == 0x3bf) {
        FUN_009a1c10();
        return;
      }
      if (param_3 == 0x264) {
switchD_0090a2e3_caseD_8a1:
        if (*(int *)(param_2 + 0x228) == 0x29f) {
          iVar1 = *(int *)(param_1 + 0x124);
          piVar3 = (int *)FUN_00aae9af(0x10);
          if (piVar3 == (int *)0x0) {
            piVar3 = (int *)0x0;
          }
          else {
            *piVar3 = param_3;
            piVar3[1] = iVar1;
            piVar3[2] = 0;
            *(undefined1 *)(piVar3 + 3) = 0;
          }
          iVar2 = *(int *)(param_1 + 0x128);
          *(int **)(param_1 + 0x124) = piVar3;
          *(int *)(param_1 + 0x128) = iVar2 + 1;
          if (iVar2 == 0) {
            *(int **)(param_1 + 0x120) = piVar3;
            return;
          }
          *(int **)(iVar1 + 8) = piVar3;
        }
        return;
      }
      if (param_3 == 0x265) {
        FUN_009a1bb0(param_1 + 0x1cc);
        return;
      }
      if (param_3 == 0x2a5) {
        (**(code **)(*(int *)(param_1 + 0x68) + 0xc))(param_2);
        return;
      }
    }
    else {
      if (param_3 == 0x6c2) goto switchD_0090a2e3_caseD_8a1;
      if (param_3 == 0x713) {
        FUN_009a1bb0(param_1 + 0x130);
        return;
      }
      if (param_3 == 0x798) {
        FUN_009a1bb0(param_1 + 0x132);
        return;
      }
    }
  }
  else if (param_3 < 0x9b3) {
    if (param_3 == 0x9b2) {
      FUN_009a1bb0(param_1 + 0x1d4);
      return;
    }
    switch(param_3) {
    case 0x89b:
      FUN_009a1ce0();
      return;
    case 0x89c:
      FUN_009a1ce0();
      return;
    case 0x89d:
      FUN_009a1ce0();
      return;
    case 0x89e:
      FUN_009a1750(param_1 + 0x138,0);
      return;
    case 0x89f:
      FUN_009a1750(param_1 + 0x154,0);
      return;
    case 0x8a0:
      FUN_009a1750(param_1 + 0x170,0);
      return;
    case 0x8a1:
    case 0x8a2:
    case 0x8a3:
    case 0x8a4:
    case 0x8a5:
    case 0x8a7:
    case 0x8a8:
    case 0x8aa:
    case 0x8da:
    case 0x942:
    case 0x975:
    case 0x976:
      goto switchD_0090a2e3_caseD_8a1;
    case 0x8bf:
      (**(code **)(*(int *)(param_1 + 0xa0) + 0xc))(param_2);
      return;
    case 0x8c0:
      (**(code **)(*(int *)(param_1 + 0x84) + 0xc))(param_2);
      return;
    case 0x8cf:
      (**(code **)(*(int *)(param_1 + 0x18c) + 0xc))(param_2);
      return;
    case 0x8d0:
      (**(code **)(*(int *)(param_1 + 0x30) + 0xc))(param_2);
      return;
    case 0x8d1:
      (**(code **)(*(int *)(param_1 + 0xd8) + 0xc))(param_2);
      return;
    case 0x8e1:
      FUN_009a1ce0();
      return;
    case 0x8ec:
      FUN_009a1c10();
      return;
    case 0x909:
      FUN_009a1ce0();
      return;
    case 0x90a:
      FUN_009a1ce0();
      return;
    case 0x90b:
      FUN_009a1ce0();
      return;
    case 0x90c:
      FUN_009a1c10();
      return;
    case 0x910:
      FUN_009a1ce0();
      return;
    case 0x911:
      FUN_009a1ce0();
      return;
    case 0x912:
      FUN_009a1ce0();
      return;
    case 0x92a:
      (**(code **)(*(int *)(param_1 + 0x4c) + 0xc))(param_2);
      return;
    case 0x92e:
      FUN_009a1bb0(param_1 + 0x1cd);
      return;
    case 0x936:
      FUN_009a1ce0();
      return;
    case 0x941:
      (**(code **)(*(int *)(param_1 + 0xbc) + 0xc))(param_2);
      return;
    case 0x995:
      (**(code **)(*(int *)(param_1 + 0x1ac) + 0xc))(param_2);
      return;
    }
  }
  else {
    switch(param_3) {
    case 0x9e4:
      FUN_009a1bb0(param_1 + 0x133);
      return;
    case 0xa2c:
      FUN_009a1ce0();
      return;
    case 0xa2e:
      FUN_009a1bb0(param_1 + 0x1dc);
      return;
    case 0xa2f:
    case 0xa30:
    case 0xa38:
    case 0xa4c:
      goto switchD_0090a2e3_caseD_8a1;
    }
  }
  FUN_009a0f40();
  return;
}

