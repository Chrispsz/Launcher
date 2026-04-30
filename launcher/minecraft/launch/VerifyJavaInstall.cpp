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

    if (!minecraftComponent) {
        emitFailed(tr("Não foi possível encontrar o componente Minecraft."));
        return;
    }

    // Requisito Java 25 (26.1+)
    if (minecraftComponent->getReleaseDateTime() >= g_VersionFilterData.java25BeginsDate) {
        if (javaVersion.major() < 25) {
            emit logLine(tr("Minecraft 26.1 e superior requerem Java 25 (detectado: Java %1)")
                            .arg(javaVersion.toString()),
                         MessageLevel::Fatal);
            emitFailed(tr("Minecraft 26.1 e superior requerem Java 25. "
                          "Java %1 detectado — instale JDK 25 ou superior.")
                            .arg(javaVersion.toString()));
            return;
        }
        // Aviso: JDK muito acima de 25 pode causar problemas
        if (javaVersion.major() > 25) {
            emit logLine(tr("AVISO: Java %1 detectado. Minecraft 26.1+ é compatível com Java 25. "
                            "JDKs mais novos podem causar instabilidade.")
                            .arg(javaVersion.toString()),
                         MessageLevel::Warning);
        }
    }
    // Requisito Java 21 (24w14a+)
    else if (minecraftComponent->getReleaseDateTime() >= g_VersionFilterData.java21BeginsDate) {
        if (javaVersion.major() < 21) {
            emit logLine(tr("Minecraft 24w14a e superior requerem Java 21 (detectado: Java %1)")
                            .arg(javaVersion.toString()),
                         MessageLevel::Fatal);
            emitFailed(tr("Minecraft 24w14a e superior requerem Java 21. "
                          "Java %1 detectado — instale JDK 21 ou superior.")
                            .arg(javaVersion.toString()));
            return;
        }
    }
    // Requisito Java 17 (1.18 Pre-Release 2+)
    else if (minecraftComponent->getReleaseDateTime() >= g_VersionFilterData.java17BeginsDate) {
        if (javaVersion.major() < 17) {
            emit logLine(tr("Minecraft 1.18 Pre-Release 2 e superior requerem Java 17 (detectado: Java %1)")
                            .arg(javaVersion.toString()),
                         MessageLevel::Fatal);
            emitFailed(tr("Minecraft 1.18 Pre-Release 2 e superior requerem Java 17. "
                          "Java %1 detectado — instale JDK 17 ou superior.")
                            .arg(javaVersion.toString()));
            return;
        }
    }
    // Requisito Java 16 (21w19a+)
    else if (minecraftComponent->getReleaseDateTime() >= g_VersionFilterData.java16BeginsDate) {
        if (javaVersion.major() < 16) {
            emit logLine(tr("Minecraft 21w19a e superior requerem Java 16 (detectado: Java %1)")
                            .arg(javaVersion.toString()),
                         MessageLevel::Fatal);
            emitFailed(tr("Minecraft 21w19a e superior requerem Java 16. "
                          "Java %1 detectado — instale JDK 16 ou superior.")
                            .arg(javaVersion.toString()));
            return;
        }
    }
    // Requisito Java 8 (17w13a+)
    else if (minecraftComponent->getReleaseDateTime() >= g_VersionFilterData.java8BeginsDate) {
        if (javaVersion.major() < 8) {
            emit logLine(tr("Minecraft 17w13a e superior requerem Java 8 (detectado: Java %1)")
                            .arg(javaVersion.toString()),
                         MessageLevel::Fatal);
            emitFailed(tr("Minecraft 17w13a e superior requerem Java 8. "
                          "Java %1 detectado — instale JDK 8 ou superior.")
                            .arg(javaVersion.toString()));
            return;
        }
    }

    emitSucceeded();
}
