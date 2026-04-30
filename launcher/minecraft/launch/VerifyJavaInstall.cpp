/*
 * Copyright 2021 Jamie Mansfield <jmansfield@cadixdev.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "VerifyJavaInstall.h"

#include <launch/LaunchTask.h>
#include <minecraft/MinecraftInstance.h>
#include <minecraft/PackProfile.h>
#include <minecraft/VersionFilterData.h>

#ifdef major
    #undef major
#endif
#ifdef minor
    #undef minor
#endif

void VerifyJavaInstall::executeTask() {
    auto m_inst = std::dynamic_pointer_cast<MinecraftInstance>(m_parent->instance());

    auto javaVersion = m_inst->getJavaVersion();
    auto minecraftComponent = m_inst->getPackProfile()->getComponent("net.minecraft");

    // Requisito Java 21
    if (minecraftComponent->getReleaseDateTime() >= g_VersionFilterData.java21BeginsDate) {
        if (javaVersion.major() < 21) {
            emit logLine("Minecraft 24w14a e superior requerem Java 21",
                         MessageLevel::Fatal);
            emitFailed(tr("Minecraft 24w14a e superior requerem Java 21"));
            return;
        }
        // Aviso: JDK acima de 21 pode causar crashes (SIGSEGV) — Minecraft é feito para JDK 21
        if (javaVersion.major() > 21) {
            emit logLine(tr("AVISO: Java %1 detectado, mas Minecraft é compatível com Java 21. "
                            "JDKs mais novos podem causar crashes (SIGSEGV/exit code 11). "
                            "Recomendamos usar Java 21 LTS.")
                            .arg(javaVersion.toString()),
                         MessageLevel::Warning);
        }
    }
    // Requisito Java 17
    if (minecraftComponent->getReleaseDateTime() >= g_VersionFilterData.java17BeginsDate) {
        if (javaVersion.major() < 17) {
            emit logLine("Minecraft 1.18 Pre-Release 2 e superior requerem Java 17",
                         MessageLevel::Fatal);
            emitFailed(tr("Minecraft 1.18 Pre-Release 2 e superior requerem Java 17"));
            return;
        }
        // Aviso para versões que requerem Java 17 mas usam JDK mais novo
        if (minecraftComponent->getReleaseDateTime() < g_VersionFilterData.java21BeginsDate && javaVersion.major() > 17) {
            emit logLine(tr("AVISO: Java %1 detectado, mas esta versão do Minecraft é compatível com Java 17. "
                            "Recomendamos usar Java 17 LTS.")
                            .arg(javaVersion.toString()),
                         MessageLevel::Warning);
        }
    }
    // Requisito Java 16
    else if (minecraftComponent->getReleaseDateTime() >= g_VersionFilterData.java16BeginsDate) {
        if (javaVersion.major() < 16) {
            emit logLine("Minecraft 21w19a e superior requerem Java 16",
                         MessageLevel::Fatal);
            emitFailed(tr("Minecraft 21w19a e superior requerem Java 16"));
            return;
        }
    }
    // Requisito Java 8
    else if (minecraftComponent->getReleaseDateTime() >= g_VersionFilterData.java8BeginsDate) {
        if (javaVersion.major() < 8) {
            emit logLine("Minecraft 17w13a e superior requerem Java 8",
                         MessageLevel::Fatal);
            emitFailed(tr("Minecraft 17w13a e superior requerem Java 8"));
            return;
        }
    }

    emitSucceeded();
}
