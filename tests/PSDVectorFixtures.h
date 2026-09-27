#pragma once
#include <QByteArray>
#include <QSizeF>
#include <map>
#include <QString>

// Photoshop shape extras, as Swift's PSDVectorFixtures holds them.
namespace PSDVectorFixtures {
inline const QSizeF canvas(1920, 1080);

inline std::map<QString, QByteArray> circle()
{
    return {
        {QStringLiteral("vmsk"), QByteArray::fromBase64(
                                       "AAAAAwAAAAAABgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAABAAEAAAAAAAAAAAAAAAAAAA"
                                       "AAAAAAAQDHWSAAXn4FAMdZIABSZmYAx1kgAEZOxwABALX4sAA8iIgAoHlcADyIiACK+gkAPIiIAAEAeZmZAEZOxwB5mZkAUmZmAHmZmQBefgUA"
                                       "AQCK+gkAaEREAKB5XABoREQAtfiwAGhERAAA")},
        {QStringLiteral("SoCo"), QByteArray::fromBase64(
                                       "AAAAEAAAAAEAAAAAAABudWxsAAAAAQAAAABDbHIgT2JqYwAAAAEAAAAAAABSR0JDAAAAAwAAAABSZCAgZG91YgAAAAAAAAAAAAAAAEdybiBkb3"
                                       "ViQFuAAAAAAAAAAAAAQmwgIGRvdWJAb+AAAAAAAA==")},
        {QStringLiteral("vstk"), QByteArray::fromBase64(
                                       "AAAAEAAAAAEAAAAAAAtzdHJva2VTdHlsZQAAABAAAAASc3Ryb2tlU3R5bGVWZXJzaW9ubG9uZwAAAAIAAAANc3Ryb2tlRW5hYmxlZGJvb2wAAA"
                                       "AAC2ZpbGxFbmFibGVkYm9vbAEAAAAUc3Ryb2tlU3R5bGVMaW5lV2lkdGhVbnRGI1B4bEAjvQfpGNNeAAAAGXN0cm9rZVN0eWxlTGluZURhc2hP"
                                       "ZmZzZXRVbnRGI1BudAAAAAAAAAAAAAAAFXN0cm9rZVN0eWxlTWl0ZXJMaW1pdGRvdWJAWQAAAAAAAAAAABZzdHJva2VTdHlsZUxpbmVDYXBUeX"
                                       "BlZW51bQAAABZzdHJva2VTdHlsZUxpbmVDYXBUeXBlAAAAEnN0cm9rZVN0eWxlQnV0dENhcAAAABdzdHJva2VTdHlsZUxpbmVKb2luVHlwZWVu"
                                       "dW0AAAAXc3Ryb2tlU3R5bGVMaW5lSm9pblR5cGUAAAAUc3Ryb2tlU3R5bGVNaXRlckpvaW4AAAAYc3Ryb2tlU3R5bGVMaW5lQWxpZ25tZW50ZW"
                                       "51bQAAABhzdHJva2VTdHlsZUxpbmVBbGlnbm1lbnQAAAAWc3Ryb2tlU3R5bGVBbGlnbkNlbnRlcgAAABRzdHJva2VTdHlsZVNjYWxlTG9ja2Jv"
                                       "b2wAAAAAF3N0cm9rZVN0eWxlU3Ryb2tlQWRqdXN0Ym9vbAAAAAAWc3Ryb2tlU3R5bGVMaW5lRGFzaFNldFZsTHMAAAAAAAAAFHN0cm9rZVN0eW"
                                       "xlQmxlbmRNb2RlZW51bQAAAABCbG5NAAAAAE5ybWwAAAASc3Ryb2tlU3R5bGVPcGFjaXR5VW50RiNQcmNAWQAAAAAAAAAAABJzdHJva2VTdHls"
                                       "ZUNvbnRlbnRPYmpjAAAAAQAAAAAAD3NvbGlkQ29sb3JMYXllcgAAAAEAAAAAQ2xyIE9iamMAAAABAAAAAAAAUkdCQwAAAAMAAAAAUmQgIGRvdW"
                                       "JAb+AAAAAAAAAAAABHcm4gZG91YkBv4AAAAAAAAAAAAEJsICBkb3ViAAAAAAAAAAAAAAAVc3Ryb2tlU3R5bGVSZXNvbHV0aW9uZG91YkBSAAAA"
                                       "AAAA")},
    };
}

inline std::map<QString, QByteArray> rectangle()
{
    return {
        {QStringLiteral("vmsk"), QByteArray::fromBase64(
                                       "AAAAAwAAAAAABgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAABAAEAAAAAAAAAAAAAAAAAAA"
                                       "AAAAAAAQAkREQA1CIiACRERADUIiIAJEREANQiIgABAE9oSwDUIiIAT2hLANQiIgBPaEsA1CIiAAEAT2hLAH4AAABPaEsAfgAAAE9oSwB+AAAA"
                                       "AQAkREQAfgAAACRERAB+AAAAJEREAH4AAAAA")},
        {QStringLiteral("SoCo"), QByteArray::fromBase64(
                                       "AAAAEAAAAAEAAAAAAABudWxsAAAAAQAAAABDbHIgT2JqYwAAAAEAAAAAAABSR0JDAAAAAwAAAABSZCAgZG91YgAAAAAAAAAAAAAAAEdybiBkb3"
                                       "ViAAAAAAAAAAAAAAAAQmwgIGRvdWIAAAAAAAAAAA==")},
        {QStringLiteral("vstk"), QByteArray::fromBase64(
                                       "AAAAEAAAAAEAAAAAAAtzdHJva2VTdHlsZQAAABAAAAASc3Ryb2tlU3R5bGVWZXJzaW9ubG9uZwAAAAIAAAANc3Ryb2tlRW5hYmxlZGJvb2wBAA"
                                       "AAC2ZpbGxFbmFibGVkYm9vbAEAAAAUc3Ryb2tlU3R5bGVMaW5lV2lkdGhVbnRGI1B4bEAjvQfpGNNeAAAAGXN0cm9rZVN0eWxlTGluZURhc2hP"
                                       "ZmZzZXRVbnRGI1BudAAAAAAAAAAAAAAAFXN0cm9rZVN0eWxlTWl0ZXJMaW1pdGRvdWJAWQAAAAAAAAAAABZzdHJva2VTdHlsZUxpbmVDYXBUeX"
                                       "BlZW51bQAAABZzdHJva2VTdHlsZUxpbmVDYXBUeXBlAAAAEnN0cm9rZVN0eWxlQnV0dENhcAAAABdzdHJva2VTdHlsZUxpbmVKb2luVHlwZWVu"
                                       "dW0AAAAXc3Ryb2tlU3R5bGVMaW5lSm9pblR5cGUAAAAUc3Ryb2tlU3R5bGVNaXRlckpvaW4AAAAYc3Ryb2tlU3R5bGVMaW5lQWxpZ25tZW50ZW"
                                       "51bQAAABhzdHJva2VTdHlsZUxpbmVBbGlnbm1lbnQAAAAWc3Ryb2tlU3R5bGVBbGlnbkNlbnRlcgAAABRzdHJva2VTdHlsZVNjYWxlTG9ja2Jv"
                                       "b2wAAAAAF3N0cm9rZVN0eWxlU3Ryb2tlQWRqdXN0Ym9vbAAAAAAWc3Ryb2tlU3R5bGVMaW5lRGFzaFNldFZsTHMAAAAAAAAAFHN0cm9rZVN0eW"
                                       "xlQmxlbmRNb2RlZW51bQAAAABCbG5NAAAAAE5ybWwAAAASc3Ryb2tlU3R5bGVPcGFjaXR5VW50RiNQcmNAWQAAAAAAAAAAABJzdHJva2VTdHls"
                                       "ZUNvbnRlbnRPYmpjAAAAAQAAAAAAD3NvbGlkQ29sb3JMYXllcgAAAAEAAAAAQ2xyIE9iamMAAAABAAAAAAAAUkdCQwAAAAMAAAAAUmQgIGRvdW"
                                       "JAb+AAAAAAAAAAAABHcm4gZG91YkBv4AAAAAAAAAAAAEJsICBkb3ViAAAAAAAAAAAAAAAVc3Ryb2tlU3R5bGVSZXNvbHV0aW9uZG91YkBSAAAA"
                                       "AAAA")},
    };
}

}
