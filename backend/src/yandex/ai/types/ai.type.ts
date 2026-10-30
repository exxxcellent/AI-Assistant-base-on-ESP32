export interface AiTextGeneration {
    modelUri: string;
    completionOptions: {
        stream: boolean;
        temperature: number;
        maxTokens: string;
        reasoningOptions: {
            mode: string;
        };
    };
    messages: [
        {
            role: string;
            // Includes only one of the fields `text`, `toolCallList`, `toolResultList`
            text: string;
            // toolCallList: {
            //     toolCalls: [
            //         {
            //             // Includes only one of the fields `functionCall`
            //             functionCall: {
            //                 name: string;
            //                 arguments: object;
            //             };
            //             // end of the list of possible fields
            //         },
            //     ];
            // };
            // toolResultList: {
            //     toolResults: [
            //         {
            //             // Includes only one of the fields `functionResult`
            //             functionResult: {
            //                 name: string;
            //                 // Includes only one of the fields `content`
            //                 content: string;
            //                 // end of the list of possible fields
            //             };
            //             // end of the list of possible fields
            //         },
            //     ];
            // };
            // end of the list of possible fields
        },
    ];
    tools: [
        {
            // Includes only one of the fields `function`
            function: {
                name: string;
                description: string;
                parameters: object;
                strict: boolean;
            };
            // end of the list of possible fields
        },
    ];
    // Includes only one of the fields `jsonObject`, `jsonSchema`
    jsonObject: boolean;
    jsonSchema: {
        schema: object;
    };
    // end of the list of possible fields
    parallelToolCalls: boolean;
    toolChoice: {
        // Includes only one of the fields `mode`, `functionName`
        mode: string;
        functionName: string;
        // end of the list of possible fields
    };
}
