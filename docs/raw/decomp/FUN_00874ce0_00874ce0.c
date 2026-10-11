
void __thiscall FUN_00874ce0(int param_1,int param_2,int param_3)

{
  char cVar1;
  char *pcVar2;
  
  if (param_3 < 0x7e0) {
    if (param_3 == 0x7df) {
      pcVar2 = (char *)(param_2 + 0x22c);
      do {
        cVar1 = *pcVar2;
        pcVar2 = pcVar2 + 1;
      } while (cVar1 != '\0');
      FUN_00409350((char *)(param_2 + 0x22c),(int)pcVar2 - (param_2 + 0x22d));
      return;
    }
    if (param_3 < 0x6a7) {
      if (param_3 == 0x6a6) {
        *(bool *)(param_1 + 0x7c) = *(int *)(param_2 + 0x228) == 0x29f;
        return;
      }
      if (param_3 == 0x58) {
        (**(code **)(*(int *)(param_1 + 0x5c) + 0xc))(param_2);
        return;
      }
      if (param_3 == 0x496) {
        (**(code **)(*(int *)(param_1 + 0x110) + 0xc))(param_2);
        return;
      }
    }
    else {
      if (param_3 == 0x729) {
        (**(code **)(*(int *)(param_1 + 0x24) + 0xc))(param_2);
        return;
      }
      if (param_3 == 0x72a) {
        (**(code **)(*(int *)(param_1 + 0x40) + 0xc))(param_2);
        return;
      }
    }
  }
  else {
    switch(param_3) {
    case 0x9ee:
      FUN_004012f0(param_1 + 0xd8);
      return;
    case 0x9ef:
      FUN_004012f0(param_1 + 0xbc);
      return;
    case 0x9f0:
      FUN_004012f0(param_1 + 0xa0);
      break;
    case 0xa21:
      FUN_009a1bb0(param_1 + 0x9c);
      return;
    case 0xa25:
      FUN_004012f0(param_1 + 0xf4);
      return;
    }
  }
  return;
}

