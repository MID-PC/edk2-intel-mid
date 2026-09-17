
[Rule.Common.SEC]
  FILE SEC = $(NAMED_GUID) RELOCS_STRIPPED FIXED {
    TE  TE    Align = Auto  $(INF_OUTPUT)/$(MODULE_NAME).efi
  }

[Rule.Common.PEI_CORE]
  FILE PEI_CORE = $(NAMED_GUID) FIXED {
    TE     TE                   $(INF_OUTPUT)/$(MODULE_NAME).efi
    UI     STRING ="$(MODULE_NAME)" Optional
  }

[Rule.Common.PEIM]
  FILE PEIM = $(NAMED_GUID) FIXED {
     PEI_DEPEX PEI_DEPEX Optional        $(INF_OUTPUT)/$(MODULE_NAME).depex
     TE       TE                         $(INF_OUTPUT)/$(MODULE_NAME).efi
     UI       STRING="$(MODULE_NAME)" Optional
  }

[Rule.Common.DXE_CORE]
  FILE DXE_CORE = $(NAMED_GUID) {
    PE32     PE32                    $(INF_OUTPUT)/$(MODULE_NAME).efi
    UI       STRING="$(MODULE_NAME)" Optional
  }

[Rule.Common.DXE_DRIVER]
  FILE DRIVER = $(NAMED_GUID) {
    DXE_DEPEX    DXE_DEPEX Optional      $(INF_OUTPUT)/$(MODULE_NAME).depex
    PE32         PE32                    $(INF_OUTPUT)/$(MODULE_NAME).efi
    UI           STRING="$(MODULE_NAME)" Optional
  }

[Rule.Common.DXE_RUNTIME_DRIVER]
  FILE DRIVER = $(NAMED_GUID) {
    DXE_DEPEX    DXE_DEPEX Optional      $(INF_OUTPUT)/$(MODULE_NAME).depex
    PE32         PE32                    $(INF_OUTPUT)/$(MODULE_NAME).efi
    UI           STRING="$(MODULE_NAME)" Optional
  }

[Rule.Common.UEFI_DRIVER]
  FILE DRIVER = $(NAMED_GUID) {
    DXE_DEPEX    DXE_DEPEX Optional      $(INF_OUTPUT)/$(MODULE_NAME).depex
    PE32         PE32                    $(INF_OUTPUT)/$(MODULE_NAME).efi
    UI           STRING="$(MODULE_NAME)" Optional
  }

[Rule.Common.USER_DEFINED.ACPITABLE]
  FILE FREEFORM = $(NAMED_GUID) {
    RAW ACPI               Optional            |.acpi
    RAW ASL                Optional            |.aml
  }

[Rule.Common.UEFI_APPLICATION]
  FILE APPLICATION = $(NAMED_GUID) {
    PE32         PE32                    $(INF_OUTPUT)/$(MODULE_NAME).efi
    UI           STRING="$(MODULE_NAME)" Optional
  }
