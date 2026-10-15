// runInWorker.mjs
import { Worker } from 'worker_threads';
import path from 'path';

export function runInWorker(task, data = null) {
    return new Promise((resolve, reject) => {
        let workerCode;
        if (typeof task === 'function') {
            workerCode = `
                const { parentPort } = require('worker_threads');
                const userFunc = ${task.toString()};
                parentPort.on('message', async (data) => {
                    try {
                        const result = await userFunc(data);
                        parentPort.postMessage(result);
                    } catch (err) {
                        parentPort.postMessage({ error: err.message });
                    }
                });
            `;
        } else if (typeof task === 'string') {
            workerCode = `
                const { parentPort } = require('worker_threads');
                require('${task}');
            `;
        } else {
            reject(new Error('task must be a function or file path'));
            return;
        }

        const worker = new Worker(workerCode, { eval: true });
        worker.postMessage(data);
        worker.on('message', (result) => {
            if (result && result.error) {
                reject(new Error(result.error));
            } else {
                resolve(result);
            }
            worker.terminate();
        });
        worker.on('error', reject);
        worker.on('exit', (code) => {
            if (code !== 0) reject(new Error(`Worker stopped with exit code ${code}`));
        });
    });
}