#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

from __future__ import annotations

import os
from functools import partial
from os import path
from typing import Any

from extract_utils.file import File
from extract_utils.makefiles import (
    MakefilesCtx,
    ProductPackagesCtx,
    write_product_copy_files,
)
from extract_utils.module import ExtractUtilsModule, ProprietaryFile

MCUAPP_FIRMWARE_BIN = 'vendor/firmware/mcuapp_firmware.bin'
MCUAPP_FIRMWARE_TXT = 'vendor/firmware/mcuapp_firmware.txt'


def add_generated_mcuapp_firmware_txt(module: ExtractUtilsModule):
    def add_copy_fn(
        proprietary_file: ProprietaryFile,
        ctx: MakefilesCtx,
        packages_ctx: ProductPackagesCtx,
        *args: Any,
        **kwargs: Any,
    ):
        if not any(
            file.dst == MCUAPP_FIRMWARE_BIN
            for file in proprietary_file.file_list.all_files
        ):
            return

        file_path_bin = path.join(
            module.proprietary_file_vendor_path(proprietary_file),
            MCUAPP_FIRMWARE_BIN,
        )
        file_path_txt = path.join(
            module.proprietary_file_vendor_path(proprietary_file),
            MCUAPP_FIRMWARE_TXT,
        )

        os.makedirs(path.dirname(file_path_txt), exist_ok=True)

        with open(file_path_bin, 'rb') as f:
            for line in f.read().split(b'\x00'):
                if line.startswith(b'V%02d.'):
                    version = line[6:].decode()
                    break

        with open(file_path_txt, 'w', encoding='utf-8') as f:
            f.write(version)

        write_product_copy_files(
            ctx,
            packages_ctx,
            [File(MCUAPP_FIRMWARE_TXT)],
        )

    for proprietary_file in module.proprietary_files:
        proprietary_file.add_post_makefile_generation_fn(
            partial(add_copy_fn, proprietary_file)
        )
