import { PutObjectCommand, S3Client } from '@aws-sdk/client-s3';
import { BadRequestException, Injectable } from '@nestjs/common';
import type { Buffer } from 'buffer';

@Injectable()
export class S3Service {
    private staticKeyId: string;
    private staticKey: string;
    private bucket: string;
    private baseUrl: string;
    private region: string;
    private s3Client: S3Client;

    constructor() {
        this.staticKeyId = process.env.YANDEX_S3_STATIC_KEY_ID as string;
        this.staticKey = process.env.YANDEX_S3_STATIC_KEY as string;
        this.bucket = process.env.YANDEX_BUCKET as string;
        this.region = process.env.YANDEX_S3_REGION as string;
        this.baseUrl = 'https://storage.yandexcloud.net';
        this.s3Client = new S3Client({
            endpoint: this.baseUrl,
            region: this.region,
            credentials: {
                accessKeyId: this.staticKeyId,
                secretAccessKey: this.staticKey,
            },
        });
    }

    private getUri(key: string) {
        return `${this.baseUrl}/${this.bucket}/${key}`;
    }

    async putObject(file: Buffer, type: 'request' | 'response') {
        const timestamp = Date.now();
        const key = `${type}_${timestamp}.mp3`;

        const response = await this.s3Client.send(
            new PutObjectCommand({
                Bucket: this.bucket,
                Key: key,
                Body: file,
            }),
        );

        if (response.$metadata.httpStatusCode !== 200)
            throw new BadRequestException('');

        return this.getUri(key);
    }
}
