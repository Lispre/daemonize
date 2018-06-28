#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <fcntl.h>
#include <syslog.h>

#include "daemonize.h"

/* daemon process body */
static int example_daemon(void *udata)
{
    int exit = 0;
    int exit_code = EXIT_SUCCESS;
    
    int sfd = -1;
    sigset_t mask;
    struct signalfd_siginfo si;

    /* open syslog */
    openlog("EXAMPLE", LOG_NDELAY, LOG_DAEMON);

    /* greeting */
    syslog(LOG_INFO, "EXAMPLE daemon started. PID: %ld", (long)getpid());

    /* create file descriptor for signal handling */
    sigemptyset(&mask);
    /* handle following signals */
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);

    /* Block signals so that they aren't handled
       according to their default dispositions */
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
    {
        closelog();
        return EXIT_FAILURE;
    }

    sfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sfd == -1)
    {
        perror("signalfd failed");
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        closelog();
        return EXIT_FAILURE;
    }
    
    /* daemon loop */
    while (!exit)
    {
        int result;
        fd_set readset;
        
        /* add signal file descriptor to set */
        FD_ZERO(&readset);
        FD_SET(sfd, &readset);
        /* One could add more file descriptors here
           and handle them accordingly if one wants to build server using
           event driven approach. */

        /* wait for data in signal file descriptor */
        result = select(FD_SETSIZE, &readset, NULL, NULL, NULL);
        if (result == -1)
        {
            syslog(LOG_ERR, "Fatal error during select() call.");
            /* low level error */
            exit_code = EXIT_FAILURE;
            break;
        }

        /* read data from signal handler file descriptor */
        if (FD_ISSET(sfd, &readset) && read(sfd, &si, sizeof(si)) > 0)
        {
            /* handle signals */
            switch (si.ssi_signo)
            {
                case SIGTERM: /* stop daemon */
                    syslog(LOG_INFO, "Got SIGTERM signal. Stopping daemon...");
                    exit = 1;
                    break;
                case SIGHUP: /* reload configuration */
                    syslog(LOG_INFO, "Got SIGHUP signal.");
                    break;
                default:
                    syslog(LOG_WARNING, "Got unexpected signal (number: %d).", si.ssi_signo);
                    break;
            }
        }
    }

    /* close signal file descriptor */
    close(sfd);
    /* remove signal handlers */
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    /* write exit code to the system log */
    syslog(LOG_INFO, "Daemon stopped with status code %d.", exit_code);
    /* close system log */
    closelog();
    
    return exit_code;
}


int main(int argc, char **argv)
{
    int exit_code = 0;
    
    pid_t pid = rundaemon(0, /* Daemon creation flags. */
                          example_daemon, NULL, /* Daemon body function and its argument. */
                          &exit_code, /* Pointer to a variable to receive daemon exit code */
                          "/tmp/example.pid"); /* Full path to the PID-file (lock). */
    switch (pid)
    {
        case -1: /* Low level error. See errno for details. */
        {
            perror("Cannot start daemon.");
            return EXIT_FAILURE;
        }
        break;
        case -2: /* Daemon already running */
        {
            fprintf(stderr,"Daemon already running.\n");
        }
        break;
        case 0: /* Daemon process. */
        {
            return exit_code; /* Return daemon exit code. */
        }
        default: /* Parent process */
        {
            printf("Parent: %ld, Daemon: %ld\n", (long)getpid(), (long)pid);
        }
        break;
    }

    return EXIT_SUCCESS;
}

