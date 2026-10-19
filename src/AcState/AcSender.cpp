#include "AcSender.h"

AcSender gAcSender;
static void onIrResolved(AcCommand resolvedCommand);
void AcSender::begin()
{
    gAcCommandController.onIrResolved(onIrResolved);
}

int AcSender::sendCommandWithAnswer(IrCodes code)
{
    int index = -1;
    // first find an empty slot in the command queue
    ControlledCommand cmd = {
        .code = code,
        .userId = 0,
        .state = 0,
        .active = false,
        .timestamp = millis(),
    };
    for (int i = 0; i < MAX_COMMAND_QUEUE_SIZE; i++)
    {
        if (!commands[i].active)
        {
            index = i;
            commands[i] = cmd;
            commands[i].active = true;
            return index;
        }
    }
    if (index < 0)
    {
        LOG_ERROR("AcSender", "Command queue is full, cannot send command 0x%08lX", (unsigned long)code);
        // return index; // queue is full
    }

    return index;
}

void processStage(ControlledCommand *command)
{
    if (command->state == 0)
    {
        // send the command
        int userId = sendIRCode(command->code);
        if (userId == 0)
        {
            LOG_ERROR("AcSender", "Failed to send command 0x%08lX, queue is full or not started", (unsigned long)command->code);
            command->state = 4; // timed out
            return;
        }
        command->state = 1;
        command->timestamp = millis();
    }
    else if (command->state == 4)
    {
        if (command->timestamp + 15000 < millis())
        {
            command->active = false; // mark as inactive after timeout
        }
    }
    else
    {
        if (command->timestamp + 5000 < millis())
        {
            command->state = 4; // timed out
            LOG_WARNING("AcSender", "Command 0x%08lX timed out at state %d waiting for confirmation", (unsigned long)command->code, command->state);
            return;
        }
    }
}

void AcSender::tick()
{
    for (int i = 0; i < MAX_COMMAND_QUEUE_SIZE; i++)
    {
        if (commands[i].active)
        {
            processStage(&commands[i]);
        }
    }
}

static void onIrResolved(AcCommand resolvedCommand)
{
    for (int i = 0; i < MAX_COMMAND_QUEUE_SIZE; i++)
    {
        if (gAcSender.commands[i].active && gAcSender.commands[i].userId == resolvedCommand.userId && gAcSender.commands[i].code == resolvedCommand.code)
        {
            auto &cmd = gAcSender.commands[i];
            if (resolvedCommand.state == State::CONFIRMED)
            {
                cmd.state = 3; // confirmed
                LOG("AcSender", "Command 0x%08lX confirmed", (unsigned long)cmd.code);
            }
            else if (resolvedCommand.state == State::TIMED_OUT)
            {
                cmd.state = 4; // timed out
                LOG_WARNING("AcSender", "Command 0x%08lX timed out waiting for confirmation", (unsigned long)cmd.code);
            }
            cmd.timestamp = millis();
            break;
        }
    }
}

ControlledCommand AcSender::getCommand(int id) const
{
    if (id < 0 || id >= MAX_COMMANDS_QUEUE_SIZE)
        return ControlledCommand{.active = false};
    return commands[id];
}

bool launchAsNonblocking(IrCodes code)
{
    gAcSender.sendCommandWithAnswer(code);
    gScheduler.everyMonotonic("t", []()
                              {
                                  static int state = 0;
                                  static int id = 0;
                                  if (state == 0)
                                  {
                                      id = gAcSender.sendCommandWithAnswer(IrCodes::AC_POWER);
                                      if (id < 0)
                                      {
                                          LOG_ERROR("AcSender", "Command queue is full, cannot send command 0x%08lX", (unsigned long)IrCodes::AC_POWER);
                                          gScheduler.remove("t");
                                      }
                                      else 
                                      {
                                          state = 1;
                                          LOG("AcSender", "Command 0x%08lX sent, waiting for confirmation", (unsigned long)IrCodes::AC_POWER);
                                      }
                                  }
                                  if (state == 1)
                                  {
                                    static int lastState = 0;
                                      auto cmd = gAcSender.getCommand(id);
                                      if (cmd.state != lastState)
                                      {
                                          lastState = cmd.state;
                                          LOG("AcSender", "Command 0x%08lX state changed to %d", (unsigned long)IrCodes::AC_POWER, cmd.state);
                                      }
                                      if (cmd.state == 3 || cmd.state == 4)
                                      {
                                          LOG("AcSender", "Command 0x%08lX completed with state %d", (unsigned long)IrCodes::AC_POWER, cmd.state);
                                          gScheduler.remove("t");
                                      }
                                      
                                  } }, 100);
    return true;
}