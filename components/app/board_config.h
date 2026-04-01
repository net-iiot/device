/**
 * Configuração do tipo de placa — escolha UMA das opções abaixo.
 *
 * PLACA 4MB (um botão, flash):
 *   - Pull-up interno do ESP32 (habilitar no código).
 *   - Timeout curto ao aguardar botão soltar (evita travamento em 3 s).
 *
 * PLACA 8MB OEE (vários botões):
 *   - Sem pull-up no hardware (resistor externo); desabilitar pull-up no código.
 *   - Aguarda até 3 s para todos os botões serem soltos.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* Descomente a placa que você está usando e comente a outra. */

#define BOARD_4MB_SINGLE_BUTTON  1   /* Placa 4MB, 1 botão, pull-up interno */
/* #define BOARD_8MB_MULTI_BUTTON  1   Placa 8MB OEE, vários botões, sem pull-up */

#if defined(BOARD_4MB_SINGLE_BUTTON) && defined(BOARD_8MB_MULTI_BUTTON)
#error "Defina apenas um tipo de placa: BOARD_4MB_SINGLE_BUTTON ou BOARD_8MB_MULTI_BUTTON"
#endif
#if !defined(BOARD_4MB_SINGLE_BUTTON) && !defined(BOARD_8MB_MULTI_BUTTON)
#error "Defina um tipo de placa em board_config.h: BOARD_4MB_SINGLE_BUTTON ou BOARD_8MB_MULTI_BUTTON"
#endif

#endif /* BOARD_CONFIG_H */
